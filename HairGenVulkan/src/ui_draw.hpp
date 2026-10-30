// ui_draw.hpp - the panel's rasteriser.
//
// The renderer is headless: there is no window and no swapchain, only a
// CPU-side RGBA8 frame that is streamed to the browser. Dear ImGui therefore
// cannot use a GPU backend here - its draw lists are walked on the CPU and
// blended straight into that frame, which also means the panel works on a
// driver stack with no presentation surface at all.
#pragma once

#include <cstdint>

struct ImDrawData;

namespace hgen {

// Composites `draw_data` into `dst` (RGBA8, straight alpha, top-down rows).
// `tex` is the ImGui font atlas as 8-bit coverage (see UiPanel::atlas_alpha).
void ui_rasterize(ImDrawData* draw_data, const unsigned char* tex, int tex_w, int tex_h, uint8_t* dst, int w,
                  int h);

}  // namespace hgen
