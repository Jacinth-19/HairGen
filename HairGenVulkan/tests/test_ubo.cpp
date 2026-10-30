// test_ubo - the frame UBO is declared twice (GLSL `FrameBlock` in
// shaders/lib/common.glsl and C++ `FrameUbo` in src/renderer.cpp) and the two
// declarations have to agree member for member, in order, because the shader
// reads the buffer as plain std430. A silent mismatch is invisible until a
// parameter moves to the wrong vector, so it is checked mechanically here
// instead of by eye.
//
// Both files are read from the source tree (the test runs with the source root
// as its working directory, like test_assets).

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Field {
  std::string type;  // "mat4" | "vec4"
  std::string name;
};

std::string slurp(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", path);
    std::exit(1);
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string between(const std::string& s, const char* a, const char* b) {
  const size_t i = s.find(a);
  const size_t j = i == std::string::npos ? i : s.find(b, i);
  if (i == std::string::npos || j == std::string::npos) {
    std::fprintf(stderr, "could not find %s ... %s\n", a, b);
    std::exit(1);
  }
  return s.substr(i, j - i);
}

bool is_ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Pulls `mat4 x;` / `vec4 y;` out of a block, ignoring whitespace differences.
std::vector<Field> glsl_fields(const std::string& block) {
  std::vector<Field> out;
  for (size_t i = 0; i < block.size();) {
    const bool mat = block.compare(i, 4, "mat4") == 0;
    const bool vec = block.compare(i, 4, "vec4") == 0;
    if ((mat || vec) && (i == 0 || !is_ident(block[i - 1]))) {
      size_t k = i + 4;
      while (k < block.size() && std::isspace(static_cast<unsigned char>(block[k]))) ++k;
      std::string name;
      while (k < block.size() && is_ident(block[k])) name += block[k++];
      while (k < block.size() && std::isspace(static_cast<unsigned char>(block[k]))) ++k;
      if (!name.empty() && k < block.size() && block[k] == ';')
        out.push_back({mat ? "mat4" : "vec4", name});
      i = k;
    } else {
      ++i;
    }
  }
  return out;
}

// Pulls `float x[16];` / `float y[4];` out of the C++ struct.
std::vector<Field> cpp_fields(const std::string& block) {
  std::vector<Field> out;
  const std::string key = "float ";
  for (size_t i = 0; i < block.size();) {
    if (block.compare(i, key.size(), key) == 0 && (i == 0 || !is_ident(block[i - 1]))) {
      size_t k = i + key.size();
      while (k < block.size() && std::isspace(static_cast<unsigned char>(block[k]))) ++k;
      std::string name;
      while (k < block.size() && is_ident(block[k])) name += block[k++];
      while (k < block.size() && std::isspace(static_cast<unsigned char>(block[k]))) ++k;
      std::string count;
      if (k < block.size() && block[k] == '[') {
        ++k;
        while (k < block.size() && std::isdigit(static_cast<unsigned char>(block[k]))) count += block[k++];
        if (k < block.size() && block[k] == ']') ++k;
      }
      if (!name.empty() && !count.empty()) out.push_back({count == "16" ? "mat4" : "vec4", name});
      i = k;
    } else {
      ++i;
    }
  }
  return out;
}

size_t field_bytes(const Field& f) { return f.type == "mat4" ? 64u : 16u; }

}  // namespace

int main() {
  const std::string glsl = slurp("shaders/lib/common.glsl");
  const std::string cpp = slurp("src/renderer.cpp");

  const std::vector<Field> g =
      glsl_fields(between(glsl, "uniform FrameBlock {", "} frame;"));
  const std::vector<Field> c =
      cpp_fields(between(cpp, "struct FrameUbo {", "};\nstatic_assert"));

  if (g.empty() || c.empty()) {
    std::fprintf(stderr, "test_ubo: parsed %zu GLSL and %zu C++ fields\n", g.size(), c.size());
    return 1;
  }
  if (g.size() != c.size()) {
    std::fprintf(stderr, "test_ubo: field count differs: GLSL %zu, C++ %zu\n", g.size(), c.size());
    return 1;
  }

  size_t offset = 0;
  for (size_t i = 0; i < g.size(); ++i) {
    if (g[i].name != c[i].name) {
      std::fprintf(stderr, "test_ubo: field %zu: GLSL '%s' vs C++ '%s' (offset %zu)\n", i,
                   g[i].name.c_str(), c[i].name.c_str(), offset);
      return 1;
    }
    if (g[i].type != c[i].type) {
      std::fprintf(stderr, "test_ubo: field '%s': GLSL %s vs C++ %s\n", g[i].name.c_str(),
                   g[i].type.c_str(), c[i].type.c_str());
      return 1;
    }
    offset += field_bytes(g[i]);
  }

  // std430: every member is mat4/vec4, so the block is just the sum with no
  // padding, and the C++ side must match that byte count exactly.
  if (offset != 752) {
    std::fprintf(stderr, "test_ubo: block is %zu bytes, expected 752\n", offset);
    return 1;
  }

  // The centrepiece of the block must stay where the shaders expect it: the
  // five matrices first, then the vec4s in declaration order.
  if (g[0].name != "mvp" || g[4].name != "invView" || g[5].name != "camPos") {
    std::fprintf(stderr, "test_ubo: matrix block changed order (%s, %s, %s)\n",
                 g[0].name.c_str(), g[4].name.c_str(), g[5].name.c_str());
    return 1;
  }

  std::printf("test_ubo: %zu fields, %zu bytes, layouts agree\n", g.size(), offset);
  return 0;
}
