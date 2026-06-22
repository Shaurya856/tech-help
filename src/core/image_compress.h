#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {

constexpr int kDefaultMaxDimension = 1600;
constexpr int kDefaultJpegQuality = 70;

struct RecompressedImage {
    std::vector<std::uint8_t> jpeg_data;
    int width;
    int height;
};

// Bytes-based overload: handles any format stb_image supports (JPEG, PNG,
// BMP, WEBP, TGA, GIF). Used for embedded images extracted from PDFs.
RecompressedImage recompress_to_jpeg(const std::vector<std::uint8_t>& input,
                                      int max_dimension = kDefaultMaxDimension,
                                      int jpeg_quality = kDefaultJpegQuality);

// Path-based overload: dispatches by file extension. Handles everything above
// plus TIFF (via libtiff) and HEIC/HEIF (via libheif). Use this for input
// image files where the extension is known.
RecompressedImage recompress_to_jpeg(const std::string& file_path,
                                      int max_dimension = kDefaultMaxDimension,
                                      int jpeg_quality = kDefaultJpegQuality);

}  // namespace core
