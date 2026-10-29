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
                           "hair.bundles", "roots.st", "roots.base", "roots.count", "colliders",
                           "noise3d", "skin.albedo", "skin.normal", "skin.ao", "skin.spec", "mtl.names",
                           "bust.pos", "bust.nrm", "bust.idx"}) {
    if (!assets.has(name)) std::fprintf(stderr, "  missing chunk %s\n", name);
    CHECK(assets.has(name));
  }

  // Skin detail maps: a tangent-space normal (built from the supplied height
  // field), an ambient-occlusion mask and a specular mask. The raw uploaded
  // normal map is not shipped -- it is pore noise, and the exporter folds a
  // prefiltered quarter of it into the height-derived normal instead.
  {
    const AssetChunk& n = assets.get("skin.normal");
    CHECK(n.width == 512 && n.height == 512);
    CHECK(n.channels == 4);
    const AssetChunk& ao = assets.get("skin.ao");
    CHECK(ao.width == 1024 && ao.channels == 1);
    const AssetChunk& spec = assets.get("skin.spec");
    CHECK(spec.width == 1024 && spec.channels == 1);
    CHECK(!assets.has("skin.height"));  // consumed at export time, not at runtime
  }

  // Materials: the bundle ships a full Marschner LUT set per hair material.
  {
    const AssetChunk& names = assets.get("mtl.names");
    std::string list(reinterpret_cast<const char*>(names.data.data()), names.data.size());
    CHECK(list.find("std_blond") != std::string::npos);
    CHECK(list.find("rosa") != std::string::npos);
    for (const char* mat : {"std_blond", "rosa"}) {
      for (const char* lut : {"N_TT_R", "N_TRT", "N_TT_R_GI", "N_TRT_GI", "GI", "M_GI"}) {
        const std::string chunk = std::string("mtl.") + mat + "." + lut;
        if (!assets.has(chunk.c_str())) std::fprintf(stderr, "  missing chunk %s\n", chunk.c_str());
        CHECK(assets.has(chunk.c_str()));
      }
    }
    const AssetChunk& nttr = assets.get("mtl.std_blond.N_TT_R");
    CHECK(nttr.channels == 4 && nttr.width == nttr.height);
    const AssetChunk& rosa = assets.get("mtl.rosa.N_TT_R");
    CHECK(rosa.channels == 4 && rosa.width == rosa.height);
  }

  // The pedestal that replaces the torn shoulders.
  {
    const AssetChunk& bpos = assets.get("bust.pos");
    const AssetChunk& bidx = assets.get("bust.idx");
    CHECK(bpos.channels == 3 && bpos.texel_count() > 0);
    CHECK(bidx.texel_count() % 3 == 0);
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

  // Explicit root samples (july_rootsamples.js): the per-bundle prefix table
  // must tile the (s,t) array exactly, and stay inside the texture.
  const AssetChunk& roots = assets.get("roots.st");
  const AssetChunk& root_base = assets.get("roots.base");
  const AssetChunk& root_count = assets.get("roots.count");
  CHECK(roots.channels == 2);
  CHECK(root_base.texel_count() == bundles.texel_count());
  CHECK(root_count.texel_count() == bundles.texel_count());
  size_t expected_base = 0;
  for (size_t b = 0; b < root_count.texel_count(); ++b) {
    CHECK(size_t(root_base.float_at(b)) == expected_base);
    CHECK(root_count.float_at(b) > 0.0f);
    expected_base += size_t(root_count.float_at(b));
  }
  CHECK(expected_base == roots.texel_count());
  CHECK(roots.texel_count() == 1000);  // july_rootsamples.js ships 1000 strips
  for (size_t i = 0; i < roots.texel_count() * 2; ++i) {
    float v = roots.float_at(i);
    CHECK(v >= 0.0f && v <= 1.0f);
  }

  // Colliders (july_colliders.js): 9 entries of two spheres, all near the head.
  const AssetChunk& colliders = assets.get("colliders");
  CHECK(colliders.channels == 8);
  CHECK(colliders.texel_count() >= 8);
  for (size_t i = 0; i < colliders.texel_count(); ++i)
    for (int c = 0; c < 8; ++c) {
      float v = colliders.float_at(i * 8 + c);
      if (c % 4 == 3) CHECK(v >= 0.0f && v < 1.0f);  // radii are in world units
      else CHECK(v > -2.0f && v < 2.0f);
    }

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
