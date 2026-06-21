#include "image_compress.h"

#include <stdexcept>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

namespace core {

namespace {

// Box-filter downscale in pixel domain: each output sample is the average of
// the corresponding rectangle of source samples. Cheap and adequate for
// photo thumbnailing (no ringing, acceptable sharpness at 1/2x–1/4x scales).
std::vector<unsigned char> box_downscale(const unsigned char* src,
                                          int sw, int sh,
                                          int dw, int dh,
                                          int channels) {
    std::vector<unsigned char> dst(static_cast<std::size_t>(dw) * dh * channels);
    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int sx0 = dx * sw / dw;
            int sy0 = dy * sh / dh;
            int sx1 = (dx + 1) * sw / dw;
            int sy1 = (dy + 1) * sh / dh;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int c = 0; c < channels; c++) {
                int sum = 0, count = 0;
                for (int sy = sy0; sy < sy1; sy++) {
                    for (int sx = sx0; sx < sx1; sx++) {
                        sum += src[(sy * sw + sx) * channels + c];
                        count++;
                    }
                }
                dst[(dy * dw + dx) * channels + c] =
                    static_cast<unsigned char>(sum / count);
            }
        }
    }
    return dst;
}

}  // namespace

RecompressedImage recompress_to_jpeg(const std::vector<std::uint8_t>& input,
                                      int max_dimension,
                                      int jpeg_quality) {
    // Query native channel count so we can force alpha-stripping without a
    // second full decode: anything ≥3 channels (RGB, RGBA) goes out as RGB;
    // 1-channel (grayscale) stays grayscale.
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
    if (!pixels) {
        throw std::runtime_error(std::string("image_compress: cannot decode image: ") +
                                  stbi_failure_reason());
    }

    // Downscale if either dimension exceeds max_dimension.
    std::vector<unsigned char> owned_pixels;
    int out_w = w, out_h = h;
    if (w > max_dimension || h > max_dimension) {
        if (w >= h) {
            out_w = max_dimension;
            out_h = std::max(1, h * max_dimension / w);
        } else {
            out_h = max_dimension;
            out_w = std::max(1, w * max_dimension / h);
        }
        owned_pixels = box_downscale(pixels, w, h, out_w, out_h, out_channels);
        stbi_image_free(pixels);
        pixels = nullptr;
    }

    const unsigned char* encode_src =
        owned_pixels.empty() ? pixels : owned_pixels.data();

    // Encode to JPEG in memory via a write-to-buffer callback.
    std::vector<std::uint8_t> jpeg_out;
    auto write_cb = [](void* ctx, void* data, int size) {
        auto* buf = static_cast<std::vector<std::uint8_t>*>(ctx);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buf->insert(buf->end(), bytes, bytes + size);
    };
    int ok = stbi_write_jpg_to_func(write_cb, &jpeg_out,
                                     out_w, out_h, out_channels,
                                     encode_src, jpeg_quality);

    if (pixels) stbi_image_free(pixels);

    if (!ok || jpeg_out.empty()) {
        throw std::runtime_error("image_compress: JPEG encoding failed");
    }

    return RecompressedImage{std::move(jpeg_out), out_w, out_h};
}

}  // namespace core
