// ui_draw.cpp - CPU rasteriser for ImGui draw lists (see ui_draw.hpp).
//
// ImGui is an immediate-mode library that emits triangle lists, so a backend is
// "just" a triangle rasteriser: here it is an edge-function scanliner with a
// top-left fill rule, barycentric interpolation of the vertex colour and the
// atlas UV, and the usual straight-alpha "source over" blend. Clipping is done
// by intersecting each triangle's bounding box with the current clip rect,
// which is enough because ImGui never relies on geometry being scissored
// exactly - it only needs nothing drawn outside the rect.
//
// It is deliberately free of any graphics API: the same code path runs on a GPU
// box, on lavapipe or on a machine with no Vulkan driver at all, which is what
// the panel needs since the renderer never presents a surface.
#include "ui_draw.hpp"

#include <cmath>
#include <cstring>

#include "imgui.h"

namespace hgen {
namespace {

struct Clip {
  int x0, y0, x1, y1;
};

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

inline uint8_t to_u8(float v) {
  const float s = v * 255.0f + 0.5f;
  return uint8_t(s <= 0.0f ? 0.0f : (s >= 255.0f ? 255.0f : s));
}

// Bilinear sample of the 8-bit font atlas, clamped at the edges.
inline float sample_alpha(const unsigned char* tex, int tw, int th, float u, float v) {
  float x = u * float(tw) - 0.5f;
  float y = v * float(th) - 0.5f;
  int xi = int(std::floor(x));
  int yi = int(std::floor(y));
  float fx = x - float(xi);
  float fy = y - float(yi);
  auto at = [&](int px, int py) -> float {
    px = px < 0 ? 0 : (px >= tw ? tw - 1 : px);
    py = py < 0 ? 0 : (py >= th ? th - 1 : py);
    return float(tex[size_t(py) * size_t(tw) + size_t(px)]) * (1.0f / 255.0f);
  };
  const float a00 = at(xi, yi), a10 = at(xi + 1, yi), a01 = at(xi, yi + 1), a11 = at(xi + 1, yi + 1);
  const float top = a00 + (a10 - a00) * fx;
  const float bot = a01 + (a11 - a01) * fx;
  return top + (bot - top) * fy;
}

inline void blend(uint8_t* p, float r, float g, float b, float a) {
  if (a <= 0.0f) return;
  if (a > 1.0f) a = 1.0f;
  const float ia = 1.0f - a;
  p[0] = to_u8(float(p[0]) * (1.0f / 255.0f) * ia + r * a);
  p[1] = to_u8(float(p[1]) * (1.0f / 255.0f) * ia + g * a);
  p[2] = to_u8(float(p[2]) * (1.0f / 255.0f) * ia + b * a);
  p[3] = 255;
}

void raster_triangle(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2, const unsigned char* tex,
                     int tw, int th, uint8_t* dst, int w, int h, const Clip& clip) {
  const float x0 = v0.pos.x, y0 = v0.pos.y;
  const float x1 = v1.pos.x, y1 = v1.pos.y;
  const float x2 = v2.pos.x, y2 = v2.pos.y;

  float area = edge(x0, y0, x1, y1, x2, y2);
  if (area == 0.0f) return;
  // ImGui emits both windings (its own AA geometry is not consistently CCW), so
  // normalise instead of culling: flip the edge functions for CW triangles.
  float sign = 1.0f;
  if (area < 0.0f) {
    area = -area;
    sign = -1.0f;
  }

  int minx = int(std::floor(std::fmin(x0, std::fmin(x1, x2))));
  int maxx = int(std::ceil(std::fmax(x0, std::fmax(x1, x2))));
  int miny = int(std::floor(std::fmin(y0, std::fmin(y1, y2))));
  int maxy = int(std::ceil(std::fmax(y0, std::fmax(y1, y2))));
  if (minx < clip.x0) minx = clip.x0;
  if (miny < clip.y0) miny = clip.y0;
  if (maxx > clip.x1) maxx = clip.x1;
  if (maxy > clip.y1) maxy = clip.y1;
  if (minx < 0) minx = 0;
  if (miny < 0) miny = 0;
  if (maxx > w) maxx = w;
  if (maxy > h) maxy = h;
  if (minx >= maxx || miny >= maxy) return;

  const float inv_area = 1.0f / area;
  const float u0 = v0.uv.x, v0y = v0.uv.y;
  const float u1 = v1.uv.x, v1y = v1.uv.y;
  const float u2 = v2.uv.x, v2y = v2.uv.y;
  auto col = [](unsigned int c, float out[4]) {
    out[0] = float((c >> IM_COL32_R_SHIFT) & 0xFF) * (1.0f / 255.0f);
    out[1] = float((c >> IM_COL32_G_SHIFT) & 0xFF) * (1.0f / 255.0f);
    out[2] = float((c >> IM_COL32_B_SHIFT) & 0xFF) * (1.0f / 255.0f);
    out[3] = float((c >> IM_COL32_A_SHIFT) & 0xFF) * (1.0f / 255.0f);
  };
  float c0[4], c1[4], c2[4];
  col(v0.col, c0);
  col(v1.col, c1);
  col(v2.col, c2);

  for (int y = miny; y < maxy; ++y) {
    const float py = float(y) + 0.5f;
    uint8_t* row = dst + size_t(y) * size_t(w) * 4;
    for (int x = minx; x < maxx; ++x) {
      const float px = float(x) + 0.5f;
      float w0 = sign * edge(x1, y1, x2, y2, px, py);  // opposite v0
      float w1 = sign * edge(x2, y2, x0, y0, px, py);  // opposite v1
      float w2 = sign * edge(x0, y0, x1, y1, px, py);  // opposite v2
      if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
      w0 *= inv_area;
      w1 *= inv_area;
      w2 *= inv_area;
      const float u = u0 * w0 + u1 * w1 + u2 * w2;
      const float v = v0y * w0 + v1y * w1 + v2y * w2;
      const float a = c0[3] * w0 + c1[3] * w1 + c2[3] * w2;
      if (a <= 0.0f) continue;
      // Everything the panel draws is either a solid vertex colour or text, so a
      // single atlas fetch multiplies the coverage in.
      const float cov = sample_alpha(tex, tw, th, u, v);
      const float alpha = a * cov;
      if (alpha <= 0.0039f) continue;
      const float r = c0[0] * w0 + c1[0] * w1 + c2[0] * w2;
      const float g = c0[1] * w0 + c1[1] * w1 + c2[1] * w2;
      const float b = c0[2] * w0 + c1[2] * w1 + c2[2] * w2;
      blend(row + size_t(x) * 4, r, g, b, alpha);
    }
  }
}

}  // namespace

void ui_rasterize(ImDrawData* draw_data, const unsigned char* tex, int tex_w, int tex_h, uint8_t* dst, int w,
                  int h) {
  if (!draw_data || !tex || !dst) return;
  const ImVec2 scale(1.0f, 1.0f);  // the panel is drawn in framebuffer pixels
  Clip clip{0, 0, w, h};
  for (int n = 0; n < draw_data->CmdListsCount; ++n) {
    const ImDrawList* list = draw_data->CmdLists[n];
    for (int c = 0; c < list->CmdBuffer.Size; ++c) {
      const ImDrawCmd& cmd = list->CmdBuffer[c];
      if (cmd.UserCallback) {
        // Reset-render-state callbacks are no-ops for a single-texture backend.
        continue;
      }
      clip.x0 = int(cmd.ClipRect.x * scale.x);
      clip.y0 = int(cmd.ClipRect.y * scale.y);
      clip.x1 = int(cmd.ClipRect.z * scale.x + 0.999f);
      clip.y1 = int(cmd.ClipRect.w * scale.y + 0.999f);
      if (clip.x0 < 0) clip.x0 = 0;
      if (clip.y0 < 0) clip.y0 = 0;
      if (clip.x1 > w) clip.x1 = w;
      if (clip.y1 > h) clip.y1 = h;
      if (clip.x1 <= clip.x0 || clip.y1 <= clip.y0) continue;

      const unsigned int start = cmd.IdxOffset;
      const unsigned int count = cmd.ElemCount;
      const ImDrawVert* vtx = list->VtxBuffer.Data;
      const ImDrawIdx* idx = list->IdxBuffer.Data + start;
      for (unsigned int i = 0; i + 2 < count; i += 3) {
        raster_triangle(vtx[idx[i]], vtx[idx[i + 1]], vtx[idx[i + 2]], tex, tex_w, tex_h, dst, w, h, clip);
      }
    }
  }
}

}  // namespace hgen
