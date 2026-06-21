#pragma once

#include <cstdint>
#include <vector>

namespace core {

constexpr int kDefaultMaxDimension = 1600;
constexpr int kDefaultJpegQuality = 70;

struct RecompressedImage {
    std::vector<std::uint8_t> jpeg_data;
    int width;
    int height;
};

// Decodes any supported raster image (JPEG or PNG) from memory, downscales
// to max_dimension on the long edge if larger, and re-encodes as JPEG at
// jpeg_quality. RGBA inputs have alpha dropped (composited over white).
// Throws std::runtime_error on unrecognised format or decode failure.
RecompressedImage recompress_to_jpeg(const std::vector<std::uint8_t>& input,
                                      int max_dimension = kDefaultMaxDimension,
                                      int jpeg_quality = kDefaultJpegQuality);

}  // namespace core
