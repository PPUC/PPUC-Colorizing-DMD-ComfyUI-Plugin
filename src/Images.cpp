#include "Images.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace comfy {

namespace {

void appendTo(void* context, void* data, int size)
{
    static_cast<std::string*>(context)->append(static_cast<const char*>(data),
                                                static_cast<std::size_t>(size));
}

}  // namespace

std::string encodePng(const Image& image)
{
    if (image.width == 0 || image.height == 0 || (image.channels != 3 && image.channels != 4) ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * image.channels) {
        throw std::invalid_argument("the picture's size does not match its pixels");
    }
    std::string png;
    const int stride = static_cast<int>(image.width * image.channels);
    if (!stbi_write_png_to_func(&appendTo, &png, static_cast<int>(image.width),
                                static_cast<int>(image.height),
                                static_cast<int>(image.channels), image.pixels.data(),
                                stride)) {
        throw std::invalid_argument("the picture could not be written as PNG");
    }
    return png;
}

Image decodeRgb(const std::string& data)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(reinterpret_cast<const unsigned char*>(data.data()),
                              static_cast<int>(data.size()), &width, &height, &channels, 3);
    if (!pixels) {
        throw std::invalid_argument(std::string("not a picture: ") + stbi_failure_reason());
    }
    Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.channels = 3;
    image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 3);
    stbi_image_free(pixels);
    return image;
}

Image areaResize(const Image& image, uint32_t width, uint32_t height)
{
    if (image.channels != 3 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 3 ||
        image.width == 0 || image.height == 0 || width == 0 || height == 0) {
        throw std::invalid_argument("the picture cannot be resized");
    }
    if (image.width == width && image.height == height) {
        return image;
    }
    // The source pixels a target pixel covers in one direction, with how much of each.
    struct Span {
        uint32_t first = 0;
        std::vector<double> weights;
    };
    const auto spans = [](uint32_t from, uint32_t to) {
        std::vector<Span> result(to);
        const double step = static_cast<double>(from) / to;
        for (uint32_t index = 0; index < to; ++index) {
            const double start = index * step;
            const double end = (index + 1) * step;
            Span& span = result[index];
            span.first = static_cast<uint32_t>(std::floor(start));
            for (uint32_t source = span.first; source < from && source < end; ++source) {
                const double covered = std::min(end, source + 1.0) -
                                       std::max(start, static_cast<double>(source));
                span.weights.push_back(covered / step);
            }
        }
        return result;
    };
    const std::vector<Span> columns = spans(image.width, width);
    const std::vector<Span> rows = spans(image.height, height);
    Image result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            double sum[3] = {0, 0, 0};
            for (std::size_t dy = 0; dy < rows[y].weights.size(); ++dy) {
                for (std::size_t dx = 0; dx < columns[x].weights.size(); ++dx) {
                    const double weight = rows[y].weights[dy] * columns[x].weights[dx];
                    const std::size_t at =
                        ((rows[y].first + dy) * image.width + columns[x].first + dx) * 3;
                    for (int c = 0; c < 3; ++c) {
                        sum[c] += weight * image.pixels[at + c];
                    }
                }
            }
            for (int c = 0; c < 3; ++c) {
                result.pixels[(static_cast<std::size_t>(y) * width + x) * 3 + c] =
                    static_cast<uint8_t>(std::clamp(std::lround(sum[c]), 0L, 255L));
            }
        }
    }
    return result;
}

}  // namespace comfy
