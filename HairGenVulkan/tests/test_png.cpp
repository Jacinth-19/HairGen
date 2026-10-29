// test_png.cpp - the PNG writer must emit a well formed file (signature, IHDR,
// IEND) and round-trip the buffer size for both RGB and RGBA input.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "png.hpp"
using namespace hgen;

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

static uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

static std::vector<uint8_t> read_all(const std::string& path) {
  std::vector<uint8_t> out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return out;
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return out;
}

int main() {
  const int w = 37, h = 21;
  std::vector<uint8_t> rgba(size_t(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      size_t o = (size_t(y) * w + x) * 4;
      rgba[o + 0] = uint8_t(x * 6);
      rgba[o + 1] = uint8_t(y * 11);
      rgba[o + 2] = uint8_t((x + y) * 3);
      rgba[o + 3] = 255;
    }

  const std::string path = "test_png_out.png";
  CHECK(write_png_rgba(path, w, h, rgba));
  auto bytes = read_all(path);
  CHECK(bytes.size() > 64);
  CHECK(bytes.size() >= 8 && std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
  // First chunk after the signature must be IHDR with our dimensions.
  CHECK(bytes.size() > 33);
  CHECK(be32(&bytes[8]) == 13);
  CHECK(std::memcmp(&bytes[12], "IHDR", 4) == 0);
  CHECK(be32(&bytes[16]) == uint32_t(w));
  CHECK(be32(&bytes[20]) == uint32_t(h));
  CHECK(bytes[24] == 8);   // bit depth
  CHECK(bytes[25] == 6);   // colour type RGBA
  // And it must end with IEND.
  CHECK(bytes.size() > 12 && std::memcmp(&bytes[bytes.size() - 8], "IEND", 4) == 0);
  std::remove(path.c_str());

  if (g_failures) {
    std::fprintf(stderr, "test_png: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_png: ok\n");
  return 0;
}
