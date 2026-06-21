#include "process_file.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

#include <hpdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "image_compress.h"

namespace core {

namespace {

namespace fs = std::filesystem;

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string extension_of(const std::string& path) {
    return to_lower(fs::path(path).extension().string());
}

bool is_image(const std::string& ext) {
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void HPDF_STDCALL on_hpdf_error(HPDF_STATUS, HPDF_STATUS, void*) {
    throw std::runtime_error("libharu error during PDF creation");
}

std::string convert_image_to_pdf(const std::string& input_path) {
    RecompressedImage img = recompress_to_jpeg(read_bytes(input_path));

    HPDF_Doc pdf = HPDF_New(on_hpdf_error, nullptr);
    if (!pdf) throw std::runtime_error("failed to initialize libharu document");

    HPDF_Image himg = HPDF_LoadJpegImageFromMem(
        pdf, img.jpeg_data.data(), static_cast<HPDF_UINT>(img.jpeg_data.size()));

    HPDF_REAL w = static_cast<HPDF_REAL>(img.width);
    HPDF_REAL h = static_cast<HPDF_REAL>(img.height);

    HPDF_Page page = HPDF_AddPage(pdf);
    HPDF_Page_SetWidth(page, w);
    HPDF_Page_SetHeight(page, h);
    HPDF_Page_DrawImage(page, himg, 0, 0, w, h);

    std::string output_path = fs::path(input_path).replace_extension(".pdf").string();
    HPDF_SaveToFile(pdf, output_path.c_str());
    HPDF_Free(pdf);
    return output_path;
}

bool is_dct_decode(const QPDFObjectHandle& filter) {
    if (filter.isName()) return filter.getName() == "/DCTDecode";
    if (filter.isArray() && filter.getArrayNItems() == 1) {
        QPDFObjectHandle item = filter.getArrayItem(0);
        return item.isName() && item.getName() == "/DCTDecode";
    }
    return false;
}

// Recompresses all DCTDecode (JPEG) image XObjects in the PDF, then writes
// with structural compression. Combines both into one QPDF pass so we only
// read and write the file once.
void optimize_pdf_in_place(const std::string& path) {
    QPDF qpdf;
    qpdf.processFile(path.c_str());

    QPDFPageDocumentHelper phelper(qpdf);
    std::set<int> visited;

    for (auto& page : phelper.getAllPages()) {
        for (auto& [name, img] : page.getImages()) {
            // Avoid recompressing the same shared XObject more than once.
            int obj_id = img.getObjectID();
            if (obj_id > 0 && !visited.insert(obj_id).second) continue;

            try {
                if (!is_dct_decode(img.getKey("/Filter"))) continue;

                auto raw = img.getRawStreamData();
                std::vector<std::uint8_t> jpeg_in(
                    raw->getBuffer(), raw->getBuffer() + raw->getSize());

                RecompressedImage recompressed = recompress_to_jpeg(jpeg_in);

                std::string new_data(
                    reinterpret_cast<const char*>(recompressed.jpeg_data.data()),
                    recompressed.jpeg_data.size());
                img.replaceStreamData(new_data,
                                       QPDFObjectHandle::newName("/DCTDecode"),
                                       QPDFObjectHandle::newNull());
                img.replaceKey("/Width",
                                QPDFObjectHandle::newInteger(recompressed.width));
                img.replaceKey("/Height",
                                QPDFObjectHandle::newInteger(recompressed.height));
            } catch (...) {
                // Skip images that can't be decoded (e.g. CMYK JPEGs) —
                // they remain as-is; the structural compression pass below
                // still applies.
            }
        }
    }

    std::string tmp_path = path + ".tmp";
    {
        QPDFWriter writer(qpdf, tmp_path.c_str());
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setCompressStreams(true);
        writer.write();
    }

    if (fs::file_size(tmp_path) < fs::file_size(path)) {
        fs::rename(tmp_path, path);
    } else {
        fs::remove(tmp_path);
    }
}

}  // namespace

ProcessResult process_file(const std::string& input_path) {
    const std::string ext = extension_of(input_path);

    if (ext == ".docx") {
        return {ProcessStatus::UnsupportedType, ""};
    }

    std::string pdf_path;
    try {
        if (is_image(ext)) {
            // Images: recompress before embedding — no need to run
            // optimize_pdf_in_place since the image is already at target
            // quality and the libharu output structure is compact.
            pdf_path = convert_image_to_pdf(input_path);
        } else if (ext == ".pdf") {
            pdf_path = input_path;
            optimize_pdf_in_place(pdf_path);
        } else {
            return {ProcessStatus::UnsupportedType, ""};
        }
    } catch (const std::exception&) {
        return {ProcessStatus::Error, ""};
    }

    return {ProcessStatus::Ok, pdf_path};
}

}  // namespace core
