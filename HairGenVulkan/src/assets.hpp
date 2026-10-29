// assets.hpp - loader for the binary bundle produced by tools/export_assets.js.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hgen {

enum AssetFormat : uint32_t {
  kAssetU8 = 0,
  kAssetU16 = 1,
  kAssetF32 = 2,
  kAssetU32 = 3,
};

struct AssetChunk {
  std::string name;
  uint32_t width = 0, height = 1, depth = 1;
  uint32_t channels = 1;
  uint32_t format = kAssetU8;
  std::vector<uint8_t> data;

  size_t bytes_per_channel() const;
  size_t texel_count() const { return size_t(width) * height * depth; }
  float float_at(size_t index) const;  // component-wise access for f32 data
};

struct Assets {
  std::vector<AssetChunk> chunks;
  uint32_t max_hair_levels = 4;

  void load(const std::string& path);
  const AssetChunk& get(const char* name) const;
  bool has(const char* name) const;
};

}  // namespace hgen
