#pragma once

// PNG files, as ComfyUI takes and gives pictures, and resizing a result to the size the editor
// takes.

#include <cstdint>
#include <string>
#include <vector>

namespace comfy {

// An 8 bit picture, row-major: 3 channels (RGB) or 4 (RGBA).
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 3;
    std::vector<uint8_t> pixels;
};

// The PNG file of the picture. Throws std::invalid_argument for a picture whose size does not
// match its pixels.
[[nodiscard]] std::string encodePng(const Image& image);
// The picture of a PNG (or another format stb_image reads), as RGB: an alpha channel is
// dropped. Throws std::invalid_argument for data that is not a picture.
[[nodiscard]] Image decodeRgb(const std::string& data);

// The RGB picture resized to width x height, each pixel the average of the area it covers
// (a whole multiple downwards averages blocks; upwards repeats pixels).
[[nodiscard]] Image areaResize(const Image& image, uint32_t width, uint32_t height);

}  // namespace comfy
