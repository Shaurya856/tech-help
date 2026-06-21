#include "process_file.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <hpdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

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

bool is_jpeg(const std::string& ext) { return ext == ".jpg" || ext == ".jpeg"; }
bool is_png(const std::string& ext) { return ext == ".png"; }

void HPDF_STDCALL on_hpdf_error(HPDF_STATUS, HPDF_STATUS, void*) {
    throw std::runtime_error("libharu error during PDF creation");
}

std::string convert_image_to_pdf(const std::string& input_path, const std::string& ext) {
    HPDF_Doc pdf = HPDF_New(on_hpdf_error, nullptr);
    if (!pdf) throw std::runtime_error("failed to initialize libharu document");

    HPDF_Image image = is_jpeg(ext)
        ? HPDF_LoadJpegImageFromFile(pdf, input_path.c_str())
        : HPDF_LoadPngImageFromFile(pdf, input_path.c_str());

    HPDF_REAL width = HPDF_Image_GetWidth(image);
    HPDF_REAL height = HPDF_Image_GetHeight(image);

    HPDF_Page page = HPDF_AddPage(pdf);
    HPDF_Page_SetWidth(page, width);
    HPDF_Page_SetHeight(page, height);
    HPDF_Page_DrawImage(page, image, 0, 0, width, height);

    std::string output_path = fs::path(input_path).replace_extension(".pdf").string();
    HPDF_SaveToFile(pdf, output_path.c_str());
    HPDF_Free(pdf);
    return output_path;
}

// QPDF only recompresses PDF container structure (object/cross-reference
// streams); it does not re-encode or downsample embedded images, so a PDF
// full of large photos may still exceed the threshold after this step.
void compress_pdf_in_place(const std::string& path) {
    QPDF qpdf;
    qpdf.processFile(path.c_str());

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

ProcessResult process_file(const std::string& input_path,
                            std::int64_t compression_threshold_mb) {
    const std::string ext = extension_of(input_path);

    if (ext == ".docx") {
        return {ProcessStatus::UnsupportedType, ""};
    }

    std::string pdf_path;
    try {
        if (is_jpeg(ext) || is_png(ext)) {
            pdf_path = convert_image_to_pdf(input_path, ext);
        } else if (ext == ".pdf") {
            pdf_path = input_path;
        } else {
            return {ProcessStatus::UnsupportedType, ""};
        }

        const std::int64_t threshold_bytes = compression_threshold_mb * 1024 * 1024;
        if (static_cast<std::int64_t>(fs::file_size(pdf_path)) > threshold_bytes) {
            compress_pdf_in_place(pdf_path);
        }
    } catch (const std::exception&) {
        return {ProcessStatus::Error, ""};
    }

    return {ProcessStatus::Ok, pdf_path};
}

}  // namespace core
