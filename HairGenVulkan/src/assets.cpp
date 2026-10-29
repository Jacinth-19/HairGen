#include "assets.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace hgen {

size_t AssetChunk::bytes_per_channel() const {
  switch (format) {
    case kAssetU8: return 1;
    case kAssetU16: return 2;
    case kAssetF32: return 4;
    case kAssetU32: return 4;
    default: return 1;
  }
}

float AssetChunk::float_at(size_t index) const {
  switch (format) {
    case kAssetF32: {
      float v;
      std::memcpy(&v, data.data() + index * 4, 4);
      return v;
    }
    case kAssetU16: {
      uint16_t v;
      std::memcpy(&v, data.data() + index * 2, 2);
      return v;
    }
    case kAssetU32: {
      uint32_t v;
      std::memcpy(&v, data.data() + index * 4, 4);
      return static_cast<float>(v);
    }
    default: return data[index];
  }
}

void Assets::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open asset bundle: " + path);
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (buf.size() < 16 || std::memcmp(buf.data(), "HGENAST2", 8) != 0)
    throw std::runtime_error("bad asset bundle magic: " + path);
  uint32_t count = 0, max_level = 0;
  std::memcpy(&count, buf.data() + 8, 4);
  std::memcpy(&max_level, buf.data() + 12, 4);
  max_hair_levels = max_level;
  size_t off = 16;
  for (uint32_t i = 0; i < count; ++i) {
    if (off + 64 > buf.size()) throw std::runtime_error("truncated asset header");
    AssetChunk c;
    std::memcpy(&c.width, buf.data() + off, 4);
    std::memcpy(&c.height, buf.data() + off + 4, 4);
    std::memcpy(&c.depth, buf.data() + off + 8, 4);
    std::memcpy(&c.channels, buf.data() + off + 12, 4);
    std::memcpy(&c.format, buf.data() + off + 16, 4);
    char name[29] = {0};
    std::memcpy(name, buf.data() + off + 20, 28);
    c.name = name;
    uint64_t chunk_off = 0, chunk_size = 0;
    std::memcpy(&chunk_off, buf.data() + off + 48, 8);
    std::memcpy(&chunk_size, buf.data() + off + 56, 8);
    if (chunk_off + chunk_size > buf.size()) throw std::runtime_error("truncated asset chunk");
    c.data.assign(buf.begin() + chunk_off, buf.begin() + chunk_off + chunk_size);
    chunks.push_back(std::move(c));
    off += 64;
  }
}

const AssetChunk& Assets::get(const char* name) const {
  for (const auto& c : chunks)
    if (c.name == name) return c;
  throw std::runtime_error(std::string("missing asset chunk: ") + name);
}

bool Assets::has(const char* name) const {
  for (const auto& c : chunks)
    if (c.name == name) return true;
  return false;
}

}  // namespace hgen
