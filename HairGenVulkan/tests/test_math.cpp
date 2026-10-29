// test_math.cpp - unit tests for the small linear-algebra helpers.
#include <cmath>
#include <cstdio>

#include "math.hpp"

using namespace hgen;

static int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))

static void test_vec() {
  vec3 a(1, 2, 3), b(4, 5, 6);
  CHECK_NEAR(dot(a, b), 32.0f, 1e-5f);
  vec3 c = cross(a, b);
  CHECK_NEAR(c.x, -3.0f, 1e-5f);
  CHECK_NEAR(c.y, 6.0f, 1e-5f);
  CHECK_NEAR(c.z, -3.0f, 1e-5f);
  CHECK_NEAR(length(normalize(vec3(3, 0, 4))), 1.0f, 1e-6f);
  CHECK_NEAR(clampf(2.0f, 0.0f, 1.0f), 1.0f, 1e-6f);
}

static void test_matrix() {
  mat4 id = mat4_identity();
  vec4 v(1, 2, 3, 1);
  vec4 r = id * v;
  CHECK_NEAR(r.x, 1.0f, 1e-6f);
  CHECK_NEAR(r.y, 2.0f, 1e-6f);
  CHECK_NEAR(r.z, 3.0f, 1e-6f);
  CHECK_NEAR(r.w, 1.0f, 1e-6f);

  // A view-projection inverted back to the original point.
  vec3 eye(0.0f, 0.0f, 3.0f), target(0.0f, 0.0f, 0.0f);
  mat4 view = mat4_look_at(eye, target, vec3(0, 1, 0));
  mat4 proj = mat4_perspective(radians(45.0f), 1.0f, 0.05f, 80.0f);
  proj.at(1, 1) = -proj.at(1, 1);  // Vulkan y-down clip space
  mat4 vp = proj * view;
  vec4 clip = vp * vec4(0.1f, 0.2f, 0.3f, 1.0f);
  CHECK(clip.w > 0.0f);
  vec4 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w, 1.0f);
  mat4 inv = mat4_invert(vp);
  vec4 back = inv * vec4(ndc.x, ndc.y, ndc.z, 1.0f);
  back = vec4(back.x / back.w, back.y / back.w, back.z / back.w, 1.0f);
  CHECK_NEAR(back.x, 0.1f, 1e-3f);
  CHECK_NEAR(back.y, 0.2f, 1e-3f);
  CHECK_NEAR(back.z, 0.3f, 1e-3f);
}

int main() {
  test_vec();
  test_matrix();
  if (g_failures) {
    std::fprintf(stderr, "test_math: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_math: ok\n");
  return 0;
}
