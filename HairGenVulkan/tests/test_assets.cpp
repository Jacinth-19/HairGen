// test_assets.cpp - validates the exported asset bundle (structure + contents).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "assets.hpp"

using namespace hgen;

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: test_assets <hairgen_assets.bin>\n");
    return 2;
  }
  Assets assets;
  try {
    assets.load(argv[1]);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "test_assets: load failed: %s\n", e.what());
    return 1;
  }

  // Every chunk the renderer asks for must be present.
  for (const char* name : {"head.pos", "head.nrm", "head.uv", "head.idx", "hairmesh.pos",
                           "hairmesh.uv", "hairmesh.uw", "hairmesh.v", "hair.clump",
                           "hair.bundles", "noise3d", "N_TT_R", "N_TRT", "N_TT_R_GI",
                           "N_TRT_GI", "GI", "M_GI", "skin.albedo"}) {
    if (!assets.has(name)) std::fprintf(stderr, "  missing chunk %s\n", name);
    CHECK(assets.has(name));
  }

  const AssetChunk& hairmesh = assets.get("hairmesh.pos");
  CHECK(hairmesh.width == 7);
  CHECK(hairmesh.height == 6);
  CHECK(hairmesh.depth == 13);
  CHECK(hairmesh.channels == 3);
  CHECK(assets.max_hair_levels == 4);

  const AssetChunk& bundles = assets.get("hair.bundles");
  CHECK(bundles.texel_count() == 22);
  CHECK(bundles.channels == 2);
  for (size_t i = 0; i < bundles.texel_count(); ++i) {
    float s = bundles.float_at(i * 2), t = bundles.float_at(i * 2 + 1);
    CHECK(s >= 0.0f && s < 7.0f);
    CHECK(t >= 0.0f && t < 6.0f);
  }

  const AssetChunk& clump = assets.get("hair.clump");
  CHECK(clump.width == 256 && clump.height == 256);
  CHECK(clump.channels == 4);
  size_t resolved = 0;
  for (size_t i = 0; i < clump.texel_count(); ++i) {
    if (clump.data[i * 4] || clump.data[i * 4 + 1] || clump.data[i * 4 + 2] || clump.data[i * 4 + 3])
      ++resolved;
  }
  // The clump map must cover a meaningful part of the hair-mesh UV layout.
  CHECK(resolved > clump.texel_count() / 4);

  const AssetChunk& idx = assets.get("head.idx");
  CHECK(idx.texel_count() > 0 && idx.texel_count() % 3 == 0);

  // Head vertices must be inside a sane bounding box (metres, head at origin).
  const AssetChunk& pos = assets.get("head.pos");
  for (size_t i = 0; i < pos.texel_count(); ++i)
    for (int c = 0; c < 3; ++c) {
      float v = pos.float_at(i * 3 + c);
      CHECK(v > -5.0f && v < 5.0f);
    }

  if (g_failures) {
    std::fprintf(stderr, "test_assets: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_assets: ok (%zu chunks)\n", assets.chunks.size());
  return 0;
}
