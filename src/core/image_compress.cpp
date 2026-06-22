#include "image_compress.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <libheif/heif.h>
#include <tiffio.h>

#include "stb_image.h"
#include "stb_image_write.h"

namespace core {

namespace {

namespace fs = std::filesystem;

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Box-filter downscale: each output sample averages the corresponding source
// rectangle. Cheap and adequate for photo thumbnailing at 1/2x–1/4x scales.
std::vector<unsigned char> box_downscale(const unsigned char* src,
                                          int sw, int sh,
                                          int dw, int dh,
                                          int channels) {
    std::vector<unsigned char> dst(static_cast<std::size_t>(dw) * dh * channels);
    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int sx0 = dx * sw / dw,  sy0 = dy * sh / dh;
            int sx1 = (dx + 1) * sw / dw, sy1 = (dy + 1) * sh / dh;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int c = 0; c < channels; c++) {
                int sum = 0, count = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++) {
                        sum += src[(sy * sw + sx) * channels + c];
                        count++;
                    }
                dst[(dy * dw + dx) * channels + c] =
                    static_cast<unsigned char>(sum / count);
            }
        }
    }
    return dst;
}

// Shared encode step: downscales raw pixels if needed then JPEG-encodes.
RecompressedImage encode_pixels(const unsigned char* pixels, int w, int h,
                                  int channels, int max_dimension, int jpeg_quality) {
    int out_w = w, out_h = h;
    std::vector<unsigned char> scaled;
    if (w > max_dimension || h > max_dimension) {
        if (w >= h) { out_w = max_dimension; out_h = std::max(1, h * max_dimension / w); }
        else        { out_h = max_dimension; out_w = std::max(1, w * max_dimension / h); }
        scaled = box_downscale(pixels, w, h, out_w, out_h, channels);
        pixels = scaled.data();
    }

    std::vector<std::uint8_t> jpeg_out;
    auto write_cb = [](void* ctx, void* data, int size) {
        auto* buf = static_cast<std::vector<std::uint8_t>*>(ctx);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buf->insert(buf->end(), bytes, bytes + size);
    };
    if (!stbi_write_jpg_to_func(write_cb, &jpeg_out, out_w, out_h, channels,
                                  pixels, jpeg_quality) || jpeg_out.empty()) {
        throw std::runtime_error("image_compress: JPEG encoding failed");
    }
    return RecompressedImage{std::move(jpeg_out), out_w, out_h};
}

RecompressedImage decode_tiff(const std::string& path, int max_dimension, int jpeg_quality) {
    struct TiffDeleter { void operator()(TIFF* p) const { TIFFClose(p); } };
    std::unique_ptr<TIFF, TiffDeleter> tif(TIFFOpen(path.c_str(), "r"));
    if (!tif) throw std::runtime_error("image_compress: cannot open TIFF: " + path);

    uint32_t w = 0, h = 0;
    TIFFGetField(tif.get(), TIFFTAG_IMAGEWIDTH,  &w);
    TIFFGetField(tif.get(), TIFFTAG_IMAGELENGTH, &h);
    if (w == 0 || h == 0) throw std::runtime_error("image_compress: TIFF has zero dimensions");

    // TIFFReadRGBAImageOriented decodes any TIFF variant to packed ABGR uint32.
    std::vector<uint32_t> raster(static_cast<std::size_t>(w) * h);
    if (!TIFFReadRGBAImageOriented(tif.get(), w, h, raster.data(), ORIENTATION_TOPLEFT, 0)) {
        throw std::runtime_error("image_compress: failed to decode TIFF: " + path);
    }

    // Unpack ABGR → RGB (drop alpha).
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; i++) {
        pixels[i * 3 + 0] = static_cast<unsigned char>(TIFFGetR(raster[i]));
        pixels[i * 3 + 1] = static_cast<unsigned char>(TIFFGetG(raster[i]));
        pixels[i * 3 + 2] = static_cast<unsigned char>(TIFFGetB(raster[i]));
    }

    return encode_pixels(pixels.data(), static_cast<int>(w), static_cast<int>(h),
                          3, max_dimension, jpeg_quality);
}

RecompressedImage decode_heic(const std::string& path, int max_dimension, int jpeg_quality) {
    struct CtxDel  { void operator()(heif_context*      p) const { heif_context_free(p); } };
    struct HndlDel { void operator()(heif_image_handle* p) const { heif_image_handle_release(p); } };
    struct ImgDel  { void operator()(heif_image*        p) const { heif_image_release(p); } };

    std::unique_ptr<heif_context, CtxDel> ctx(heif_context_alloc());

    heif_error err = heif_context_read_from_file(ctx.get(), path.c_str(), nullptr);
    if (err.code != heif_error_Ok)
        throw std::runtime_error(std::string("image_compress: cannot open HEIC: ") + err.message);

    heif_image_handle* raw_handle = nullptr;
    err = heif_context_get_primary_image_handle(ctx.get(), &raw_handle);
    if (err.code != heif_error_Ok)
        throw std::runtime_error(std::string("image_compress: cannot read HEIC image handle: ") +
                                  err.message);
    std::unique_ptr<heif_image_handle, HndlDel> handle(raw_handle);

    heif_image* raw_img = nullptr;
    err = heif_decode_image(handle.get(), &raw_img,
                             heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
    if (err.code != heif_error_Ok)
        throw std::runtime_error(std::string("image_compress: cannot decode HEIC pixels: ") +
                                  err.message);
    std::unique_ptr<heif_image, ImgDel> img(raw_img);

    int stride = 0;
    const uint8_t* data = heif_image_get_plane_readonly(
        img.get(), heif_channel_interleaved, &stride);

    int w = heif_image_handle_get_width(handle.get());
    int h = heif_image_handle_get_height(handle.get());

    // Copy to a contiguous RGB buffer (stride may be wider than w*3).
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; y++)
        std::copy(data + y * stride, data + y * stride + w * 3,
                   pixels.begin() + y * w * 3);

    return encode_pixels(pixels.data(), w, h, 3, max_dimension, jpeg_quality);
}

}  // namespace

RecompressedImage recompress_to_jpeg(const std::vector<std::uint8_t>& input,
                                      int max_dimension, int jpeg_quality) {
    int native_w, native_h, native_ch;
    if (!stbi_info_from_memory(input.data(), static_cast<int>(input.size()),
                                &native_w, &native_h, &native_ch)) {
        throw std::runtime_error(std::string("image_compress: cannot read image header: ") +
                                  stbi_failure_reason());
    }
    const int out_channels = (native_ch >= 3) ? 3 : 1;

    int w, h, ch_unused;
    unsigned char* pixels = stbi_load_from_memory(
        input.data(), static_cast<int>(input.size()), &w, &h, &ch_unused, out_channels);
    if (!pixels)
        throw std::runtime_error(std::string("image_compress: cannot decode image: ") +
                                  stbi_failure_reason());

    RecompressedImage result = encode_pixels(pixels, w, h, out_channels, max_dimension, jpeg_quality);
    stbi_image_free(pixels);
    return result;
}

RecompressedImage recompress_to_jpeg(const std::string& file_path,
                                      int max_dimension, int jpeg_quality) {
    const std::string ext = to_lower(fs::path(file_path).extension().string());

    if (ext == ".tif" || ext == ".tiff")
        return decode_tiff(file_path, max_dimension, jpeg_quality);

    if (ext == ".heic" || ext == ".heif")
        return decode_heic(file_path, max_dimension, jpeg_quality);

    // All other formats: read bytes and let stb handle them.
    std::ifstream in(file_path, std::ios::binary);
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
    return recompress_to_jpeg(bytes, max_dimension, jpeg_quality);
}

}  // namespace core
