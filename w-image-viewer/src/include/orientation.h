#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace wiv {
inline int valid_orientation(int value) noexcept { return value >= 1 && value <= 8 ? value : 1; }

inline std::pair<int, int> oriented_size(int width, int height, int orientation) noexcept
{
    return valid_orientation(orientation) >= 5 ? std::pair{height, width} : std::pair{width, height};
}

// EXIF coordinates use an upper-left origin. Copy whole pixels without resampling.
inline void orient_pixels(std::vector<uint8_t>& pixels, int& width, int& height,
                          size_t pixel_bytes, int orientation)
{
    orientation = valid_orientation(orientation);
    if (orientation == 1) return;
    const auto [out_width, out_height] = oriented_size(width, height, orientation);
    std::vector<uint8_t> output(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int dx = x, dy = y;
            switch (orientation) {
                case 2: dx = width - 1 - x; break;
                case 3: dx = width - 1 - x; dy = height - 1 - y; break;
                case 4: dy = height - 1 - y; break;
                case 5: dx = y; dy = x; break;
                case 6: dx = height - 1 - y; dy = x; break;
                case 7: dx = height - 1 - y; dy = width - 1 - x; break;
                case 8: dx = y; dy = width - 1 - x; break;
            }
            std::memcpy(output.data() + (size_t(dy) * out_width + dx) * pixel_bytes,
                        pixels.data() + (size_t(y) * width + x) * pixel_bytes, pixel_bytes);
        }
    }
    pixels.swap(output);
    width = out_width;
    height = out_height;
}
}
