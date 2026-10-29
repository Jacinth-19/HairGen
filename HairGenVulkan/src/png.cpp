#include "png.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "miniz.h"
}

namespace hgen {
namespace {

void put_u32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  put_u32(out, uint32_t(data.size()));
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  uint32_t crc = uint32_t(mz_crc32(MZ_CRC32_INIT, out.data() + start, data.size() + 4));
  put_u32(out, crc);
}

}  // namespace

std::vector<uint8_t> encode_png(int width, int height, int channels,
                                const uint8_t* pixels, int stride_bytes) {
  if (channels != 3 && channels != 4) return {};
  if (stride_bytes == 0) stride_bytes = width * channels;
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (width * channels + 1));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), pixels + size_t(y) * stride_bytes,
               pixels + size_t(y) * stride_bytes + size_t(width) * channels);
  }
  mz_ulong comp_size = mz_compressBound(mz_ulong(raw.size()));
  std::vector<uint8_t> comp(comp_size);
  if (mz_compress2(comp.data(), &comp_size, raw.data(), mz_ulong(raw.size()), MZ_BEST_COMPRESSION) != MZ_OK)
    return {};
  comp.resize(comp_size);

  std::vector<uint8_t> out;
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  out.insert(out.end(), sig, sig + 8);
  std::vector<uint8_t> ihdr;
  put_u32(ihdr, uint32_t(width));
  put_u32(ihdr, uint32_t(height));
  ihdr.push_back(8);
  ihdr.push_back(channels == 3 ? 2 : 6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", comp);
  chunk(out, "IEND", {});
  return out;
}

std::vector<uint8_t> encode_png_rgba(int width, int height, const std::vector<uint8_t>& rgba) {
  return encode_png(width, height, 4, rgba.data(), width * 4);
}

bool write_png(const std::string& path, int width, int height, int channels,
               const uint8_t* pixels, int stride_bytes) {
  if (channels != 3 && channels != 4) return false;
  if (stride_bytes == 0) stride_bytes = width * channels;
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (width * channels + 1));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), pixels + size_t(y) * stride_bytes,
               pixels + size_t(y) * stride_bytes + size_t(width) * channels);
  }
  mz_ulong comp_size = mz_compressBound(mz_ulong(raw.size()));
  std::vector<uint8_t> comp(comp_size);
  if (mz_compress2(comp.data(), &comp_size, raw.data(), mz_ulong(raw.size()), MZ_BEST_COMPRESSION) != MZ_OK)
    return false;
  comp.resize(comp_size);

  std::vector<uint8_t> out;
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  out.insert(out.end(), sig, sig + 8);
  std::vector<uint8_t> ihdr;
  put_u32(ihdr, uint32_t(width));
  put_u32(ihdr, uint32_t(height));
  ihdr.push_back(8);
  ihdr.push_back(channels == 3 ? 2 : 6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", comp);
  chunk(out, "IEND", {});

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  size_t written = std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
  return written == out.size();
}

bool write_png_rgba(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
  return write_png(path, width, height, 4, rgba.data(), width * 4);
}

}  // namespace hgen
