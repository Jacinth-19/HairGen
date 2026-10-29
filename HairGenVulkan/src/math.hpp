// math.hpp - tiny linear algebra used by the renderer (column-major mat4,
// matching GLSL/Vulkan conventions).
#pragma once

#include <cmath>
#include <cstring>

namespace hgen {

constexpr float kPi = 3.14159265358979323846f;

struct vec2 {
  float x = 0, y = 0;
  vec2() = default;
  vec2(float a, float b) : x(a), y(b) {}
};

struct vec3 {
  float x = 0, y = 0, z = 0;
  vec3() = default;
  vec3(float a) : x(a), y(a), z(a) {}
  vec3(float a, float b, float c) : x(a), y(b), z(c) {}
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
};

inline vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline vec3 operator*(vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline vec3 operator*(float s, vec3 a) { return a * s; }
inline vec3 operator*(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline vec3 operator/(vec3 a, float s) { return a * (1.0f / s); }
inline vec3& operator+=(vec3& a, vec3 b) { a = a + b; return a; }
inline vec3& operator*=(vec3& a, float s) { a = a * s; return a; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(vec3 a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(vec3 a) { return a / (length(a) + 1e-20f); }
inline vec3 mix(vec3 a, vec3 b, float t) { return a * (1 - t) + b * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float radians(float d) { return d * kPi / 180.0f; }

struct vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  vec4() = default;
  vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
  vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
  vec3 xyz() const { return {x, y, z}; }
};

struct mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
  float& at(int col, int row) { return m[col * 4 + row]; }
  float at(int col, int row) const { return m[col * 4 + row]; }
  const float* data() const { return m; }
};

inline mat4 operator*(const mat4& a, const mat4& b) {
  mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.at(k, row) * b.at(c, k);
      r.at(c, row) = s;
    }
  return r;
}

inline vec4 operator*(const mat4& a, const vec4& v) {
  vec4 r;
  r.x = a.at(0, 0) * v.x + a.at(1, 0) * v.y + a.at(2, 0) * v.z + a.at(3, 0) * v.w;
  r.y = a.at(0, 1) * v.x + a.at(1, 1) * v.y + a.at(2, 1) * v.z + a.at(3, 1) * v.w;
  r.z = a.at(0, 2) * v.x + a.at(1, 2) * v.y + a.at(2, 2) * v.z + a.at(3, 2) * v.w;
  r.w = a.at(0, 3) * v.x + a.at(1, 3) * v.y + a.at(2, 3) * v.z + a.at(3, 3) * v.w;
  return r;
}

inline mat4 mat4_identity() { return mat4{}; }

inline mat4 mat4_translate(vec3 t) {
  mat4 m;
  m.at(3, 0) = t.x;
  m.at(3, 1) = t.y;
  m.at(3, 2) = t.z;
  return m;
}

inline mat4 mat4_rotate_x(float a) {
  mat4 m;
  float c = std::cos(a), s = std::sin(a);
  m.at(1, 1) = c; m.at(2, 1) = -s;
  m.at(1, 2) = s; m.at(2, 2) = c;
  return m;
}

inline mat4 mat4_rotate_y(float a) {
  mat4 m;
  float c = std::cos(a), s = std::sin(a);
  m.at(0, 0) = c; m.at(2, 0) = s;
  m.at(0, 2) = -s; m.at(2, 2) = c;
  return m;
}

inline mat4 mat4_scale(float s) {
  mat4 m;
  m.at(0, 0) = m.at(1, 1) = m.at(2, 2) = s;
  return m;
}

inline mat4 mat4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
  mat4 m;
  float f = 1.0f / std::tan(fovy_rad * 0.5f);
  std::memset(m.m, 0, sizeof(m.m));
  m.at(0, 0) = f / aspect;
  m.at(1, 1) = f;
  m.at(2, 2) = zfar / (znear - zfar);
  m.at(2, 3) = -1.0f;
  m.at(3, 2) = (znear * zfar) / (znear - zfar);
  return m;
}

inline mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up) {
  vec3 f = normalize(target - eye);
  vec3 s = normalize(cross(f, up));
  vec3 u = cross(s, f);
  mat4 m;
  m.at(0, 0) = s.x; m.at(1, 0) = s.y; m.at(2, 0) = s.z;
  m.at(0, 1) = u.x; m.at(1, 1) = u.y; m.at(2, 1) = u.z;
  m.at(0, 2) = -f.x; m.at(1, 2) = -f.y; m.at(2, 2) = -f.z;
  m.at(3, 0) = -dot(s, eye);
  m.at(3, 1) = -dot(u, eye);
  m.at(3, 2) = dot(f, eye);
  return m;
}

inline mat4 mat4_invert(const mat4& a) {
  const float* m = a.m;
  mat4 out;
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::fabs(det) < 1e-20f) return mat4{};
  det = 1.0f / det;
  for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * det;
  return out;
}

}  // namespace hgen
