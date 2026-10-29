// png.hpp - minimal PNG writer (miniz for deflate).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hgen {

// Writes 8-bit RGB (3 channels) or RGBA (4 channels) rows top-down.
bool write_png(const std::string& path, int width, int height, int channels,
               const uint8_t* pixels, int stride_bytes = 0);

// Convenience: RGBA8 buffer as produced by the renderer.
bool write_png_rgba(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba);

}  // namespace hgen
