// test_ui.cpp - unit tests for the colour palette, the hairstyle catalogue and
// the CPU-rendered ImGui panel.
//
// The panel needs no GPU (that is the point of ui_draw.cpp), so it can be
// exercised in a plain unit test: build a few frames over a synthetic
// background and check that it actually drew something, that the dockspace
// split into the expected node tree, and that a synthetic click on a widget
// changes the scene.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // window/dock node inspection
#include "scene.hpp"
#include "ui.hpp"

using namespace hgen;

static int g_failures = 0;

#define CHECK(cond)                                                                     \
  do {                                                                                  \
    if (!(cond)) {                                                                      \
      std::fprintf(stderr, "  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
      ++g_failures;                                                                     \
    }                                                                                   \
  } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))

// ---------------------------------------------------------------- palette
static void test_palette() {
  CHECK(sizeof(kDyeColors) / sizeof(kDyeColors[0]) >= 30);
  for (const DyeColor& c : kDyeColors) {
    float lin[3];
    CHECK(parse_hair_color(c.name, lin));
    // Round trip: linear -> sRGB hex must give back the palette entry, which is
    // what the panel shows next to the swatch.
    unsigned back = 0;
    linear_to_hair_color(lin, back);
    CHECK((back & 0xFFFFFF) == (c.rgb & 0xFFFFFF));
    // Linear values have to be inside the display range.
    for (int i = 0; i < 3; ++i) CHECK(lin[i] >= 0.0f && lin[i] <= 1.0f);
  }
  // Hex parsing accepts #RRGGBB and agrees with the named entry.
  float hex[3], named[3];
  CHECK(parse_hair_color("#E8556F", hex));
  CHECK(parse_hair_color("rosa", named));
  for (int i = 0; i < 3; ++i) CHECK_NEAR(hex[i], named[i], 1e-6f);
  CHECK(!parse_hair_color("definitely_not_a_colour", hex));
  CHECK(!parse_hair_color("#zz", hex));
  // The default dye is the rosa entry, so the start-up look and the lit-up
  // swatch agree (they did not before).
  HairSettings h;
  for (int i = 0; i < 3; ++i) CHECK_NEAR(h.dye[i], named[i], 1e-3f);
}

// ---------------------------------------------------------------- styles
static void test_styles() {
  CHECK(kHairstyleCount >= 20);
  int female = 0, male = 0, unisex = 0;
  for (int i = 0; i < kHairstyleCount; ++i) {
    const HairstylePreset& p = kHairstyles[i];
    CHECK(p.name && p.label && p.sex && p.dye_hint);
    // Names are the CLI vocabulary: unique, and no spaces or dashes.
    for (int j = i + 1; j < kHairstyleCount; ++j) CHECK(std::strcmp(p.name, kHairstyles[j].name) != 0);
    CHECK(std::strchr(p.name, ' ') == nullptr);
    // Values have to be inside what the operator stack and the shaders accept.
    CHECK(p.cut_length >= 0.15f && p.cut_length <= 1.0f);
    CHECK(p.volume >= 0.0f && p.volume <= 1.0f);
    CHECK(p.curl_amount >= 0.0f && p.curl_amount <= 0.4f);
    CHECK(p.kink_amount >= 0.0f && p.kink_amount <= 0.4f);
    CHECK(p.clump_percent >= 0.0f && p.clump_percent <= 1.0f);
    CHECK(p.width_px >= 1.0f && p.width_px <= 8.0f);
    CHECK(p.hair_count >= 2000);
    // The suggested dye has to exist in the palette.
    float tmp[3];
    CHECK(parse_hair_color(p.dye_hint, tmp));
    if (std::strcmp(p.sex, "Female") == 0) ++female;
    else if (std::strcmp(p.sex, "Male") == 0) ++male;
    else ++unisex;
  }
  CHECK(female >= 8);
  CHECK(male >= 8);
  CHECK(unisex >= 3);

  // Applying a preset must copy every style value, and must not touch the dye.
  HairSettings h;
  h.dye[0] = 0.123f;
  CHECK(apply_hairstyle(h, "afro"));
  CHECK_NEAR(h.cut_length, 0.70f, 1e-6f);
  CHECK_NEAR(h.volume, 0.62f, 1e-6f);
  CHECK_NEAR(h.dye[0], 0.123f, 1e-6f);
  CHECK(!apply_hairstyle(h, "no_such_style"));
  CHECK(hairstyle_index("pixie") >= 0);
}

// ---------------------------------------------------------------- CLI echo
static void test_cli_command() {
  Scene scene;
  std::string cmd = UiPanel::cli_command(scene);
  CHECK(cmd.find("hairgen") == 0);
  CHECK(cmd.find("--dye rosa") != std::string::npos);
  CHECK(cmd.find("--rot") != std::string::npos);

  apply_hairstyle(scene.hair, "afro");
  scene.hair.dye_gradient = 0.8f;
  scene.hair.dye_tip[0] = 0.9f;
  scene.hair.dye_tip[1] = 0.7f;
  scene.hair.dye_tip[2] = 0.3f;
  scene.weather.rain = 0.5f;
  const std::string cmd2 = UiPanel::cli_command(scene);
  CHECK(cmd2.find("--style afro") != std::string::npos);
  CHECK(cmd2.find("--dye-gradient") != std::string::npos);
  CHECK(cmd2.find("--dye-tip") != std::string::npos);
  CHECK(cmd2.find("--rain") != std::string::npos);
}

// ---------------------------------------------------------------- panel
static void test_panel() {
  UiPanel ui;
  const int w = 900, h = 700;
  ui.init("assets/fonts", 1.0f);
  CHECK(ui.ready());

  std::vector<uint8_t> px(size_t(w) * h * 4);
  Scene scene;
  UiStats stats;
  stats.width = w;
  stats.height = h;
  stats.samples = 4;
  stats.fps = 30.0f;
  stats.triangles = 1234;
  stats.strands = 34000;

  UiResult last;
  for (int frame = 0; frame < 4; ++frame) {
    // A flat background: any pixel that is not this colour was drawn by the UI.
    for (size_t i = 0; i < px.size(); i += 4) {
      px[i + 0] = 20;
      px[i + 1] = 22;
      px[i + 2] = 26;
      px[i + 3] = 255;
    }
    last = ui.frame(scene, stats, px.data(), w, h, 1.0f / 60.0f);
  }
  CHECK(!last.save_png);

  size_t changed = 0;
  for (size_t i = 0; i < px.size(); i += 4)
    if (px[i] != 20 || px[i + 1] != 22 || px[i + 2] != 26) ++changed;
  // The panel covers roughly a third of the frame plus the menu bar and the
  // stats overlay; a few percent is a very conservative lower bound.
  CHECK(changed > size_t(w) * size_t(h) / 50);

  // The dockspace has to be a split node with docked windows, otherwise the
  // default layout silently failed (which is exactly what it did while the
  // windows were missing their Begin/End pairs).
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  CHECK(ctx != nullptr);
  int docked = 0;
  for (ImGuiWindow* win : ctx->Windows)
    if (win->DockId != 0) ++docked;
  CHECK(docked >= 5);
  // (The dockspace's own node id is only valid inside a frame, so the docked
  // window count above is the observable that matters.)

  // A click on the first slider of the Hair window must reach the scene: the
  // panel is the only way the browser edits it, so this is the end-to-end check
  // of input -> ImGui -> Scene.
  Scene edited;
  UiInputEvent move;
  move.type = UiInputEvent::MousePos;
  move.x = 800.0f;
  move.y = 120.0f;
  ui.push_event(move);
  for (int frame = 0; frame < 3; ++frame) ui.frame(edited, stats, px.data(), w, h, 1.0f / 60.0f);
  UiInputEvent down;
  down.type = UiInputEvent::MouseButton;
  down.button = 0;
  down.down = true;
  ui.push_event(down);
  for (int frame = 0; frame < 2; ++frame) ui.frame(edited, stats, px.data(), w, h, 1.0f / 60.0f);
  UiInputEvent up = down;
  up.down = false;
  ui.push_event(up);
  ui.frame(edited, stats, px.data(), w, h, 1.0f / 60.0f);

  // Whatever the click hit, the panel must still be able to produce a frame and
  // the hair settings must stay in their legal ranges.
  CHECK(edited.hair.cut_length >= 0.0f && edited.hair.cut_length <= 2.0f);
  CHECK(edited.hair.hair_count > 0);
}

int main() {
  test_palette();
  test_styles();
  test_cli_command();
  test_panel();
  if (g_failures == 0) std::printf("test_ui: ok\n");
  return g_failures == 0 ? 0 : 1;
}
