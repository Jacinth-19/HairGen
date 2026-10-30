// ui.cpp - the dockable Dear ImGui panel (see ui.hpp).
//
// Layout: a full-viewport dockspace with the render as its (passthrough)
// background, and every parameter group as its own dockable, resizable,
// floatable window - Hair, Colour, Styles, Lighting, Ground, Weather, Camera,
// Output. Nothing about the arrangement is hard-coded after the first frame:
// the default layout is built once through the DockBuilder API, then ImGui owns
// it, so windows can be split, tabbed, floated or closed like in any DCC tool.
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder* - the only internal API used
#include "renderer.hpp"
#include "ui_draw.hpp"
#include "ui_icons.hpp"

namespace hgen {
namespace {

// ---------------------------------------------------------------- palette
const ImVec4 kAccent(0.914f, 0.333f, 0.435f, 1.00f);       // #E8556F (rosa)
const ImVec4 kAccentDim(0.914f, 0.333f, 0.435f, 0.35f);
const ImVec4 kText(0.886f, 0.898f, 0.925f, 1.00f);
const ImVec4 kTextDim(0.588f, 0.616f, 0.667f, 1.00f);
const ImVec4 kGood(0.435f, 0.831f, 0.592f, 1.00f);
const ImVec4 kWarn(0.976f, 0.741f, 0.353f, 1.00f);

ImU32 to_u32(const ImVec4& c) { return ImGui::GetColorU32(c); }

// Width of the label column in slider rows (scaled with the panel font).
float gLabelWidth = 92.0f;

ImVec4 srgb_to_linear(const ImVec4& c) {
  auto f = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
  return ImVec4(f(c.x), f(c.y), f(c.z), c.w);
}

ImVec4 linear_to_srgb(const ImVec4& c) {
  auto f = [](float v) {
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
  };
  return ImVec4(f(c.x), f(c.y), f(c.z), c.w);
}

unsigned rgb_from_linear(const float c[3]) {
  unsigned rgb = 0;
  linear_to_hair_color(c, rgb);
  return rgb;
}

ImVec4 vec4_from_linear(const float c[3]) {
  const ImVec4 s = linear_to_srgb(ImVec4(c[0], c[1], c[2], 1.0f));
  return s;
}

void linear_from_vec4(const ImVec4& s, float out[3]) {
  const ImVec4 l = srgb_to_linear(ImVec4(s.x, s.y, s.z, 1.0f));
  out[0] = l.x;
  out[1] = l.y;
  out[2] = l.z;
}

ImVec4 with_alpha(ImVec4 c, float a) {
  c.w = a;
  return c;
}

// Lighten / darken in sRGB so the pale-natural presets behave like a colourist
// would: "a shade lighter" is a perceptual change, not a linear scale.
ImVec4 shade(const ImVec4& s, float amount) {
  float h, sv, v;
  ImGui::ColorConvertRGBtoHSV(s.x, s.y, s.z, h, sv, v);
  v = std::min(1.0f, std::max(0.0f, v + amount));
  sv = std::min(1.0f, std::max(0.0f, sv * (1.0f - amount * 0.45f)));
  ImVec4 out;
  ImGui::ColorConvertHSVtoRGB(h, sv, v, out.x, out.y, out.z);
  out.w = 1.0f;
  return out;
}

// ---------------------------------------------------------------- widgets

// A slider with the label on the left, the track filling the rest of the row,
// double-click-to-reset and an optional help tooltip. ImGui's stock layout puts
// the label to the *right* of the track, which in a narrow dock column pushes
// long labels onto a second line; a fixed label column keeps every row aligned.
// ImGui draws the slider knob *before* the value text, so a value near the
// middle of its range ends up with the number sitting on top of an opaque knob -
// legible only by luck. Instead of the knob, draw the track as a filled bar
// (the usual "fill up to the value" convention) and hide ImGui's knob: the
// number then always sits on the same dark track.
ImVec4& slider_track_colour() {
  static ImVec4 c(0.145f, 0.125f, 0.16f, 1.0f);
  return c;
}

void slider_track(float t) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::CalcItemWidth();
  const float h = ImGui::GetFrameHeight();
  const float r = ImGui::GetStyle().FrameRounding;
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(slider_track_colour()), r);
  const float fw = w * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
  if (fw > 0.5f)
    dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + h),
                      ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.42f)), r);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.045f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.075f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
}

void slider_track_end() { ImGui::PopStyleColor(5); }

bool slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f", const char* help = nullptr,
            float reset = 0.0f) {
  ImGui::PushID(label);
  const float label_w = gLabelWidth;
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kTextDim, "%s", label);
  ImGui::SameLine(label_w);
  ImGui::SetNextItemWidth(-FLT_MIN);
  slider_track(hi > lo ? (*v - lo) / (hi - lo) : 0.0f);
  bool changed = ImGui::SliderFloat("##v", v, lo, hi, fmt);
  slider_track_end();
  if (ImGui::IsItemHovered()) {
    if (help) ImGui::SetTooltip("%s\n\nRight-click to reset.", help);
    else ImGui::SetTooltip("Right-click to reset to the default.");
  }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    *v = reset;
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

bool slider_int(const char* label, int* v, int lo, int hi, const char* help = nullptr) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kTextDim, "%s", label);
  ImGui::SameLine(gLabelWidth);
  ImGui::SetNextItemWidth(-FLT_MIN);
  slider_track(hi > lo ? float(*v - lo) / float(hi - lo) : 0.0f);
  bool changed = ImGui::SliderInt("##v", v, lo, hi);
  slider_track_end();
  if (ImGui::IsItemHovered() && help) ImGui::SetTooltip("%s", help);
  ImGui::PopID();
  return changed;
}

bool toggle(const char* label, bool* v, const char* help = nullptr) {
  bool changed = ImGui::Checkbox(label, v);
  if (ImGui::IsItemHovered() && help) ImGui::SetTooltip("%s", help);
  return changed;
}

// Section header: an icon, a title, and a hairline under it.
void section(const char* icon, const char* title) {
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted(icon);
  ImGui::PopStyleColor();
  ImGui::SameLine(0.0f, 6.0f);
  ImGui::TextUnformatted(title);
  ImGui::Separator();
  ImGui::Spacing();
}

// A one-line hint in the dim colour, used to explain a control group inline.
void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void chip(const char* icon, const char* label, const ImVec4& colour) {
  ImGui::PushStyleColor(ImGuiCol_Text, colour);
  ImGui::Text("%s %s", icon, label);
  ImGui::PopStyleColor();
}

// Fills the remaining width of the current column.
float avail() { return ImGui::GetContentRegionAvail().x; }


// ---------------------------------------------------------------- input map

// Browser keyCode (legacy VK) -> ImGuiKey. Only the keys a parameter panel
// needs: text editing, navigation, modifiers and the usual shortcuts.
ImGuiKey map_key(int kc) {
  if (kc >= 65 && kc <= 90) return ImGuiKey(ImGuiKey_A + (kc - 65));
  if (kc >= 48 && kc <= 57) return ImGuiKey(ImGuiKey_0 + (kc - 48));
  if (kc >= 112 && kc <= 123) return ImGuiKey(ImGuiKey_F1 + (kc - 112));
  switch (kc) {
    case 8: return ImGuiKey_Backspace;
    case 9: return ImGuiKey_Tab;
    case 13: return ImGuiKey_Enter;
    case 16: return ImGuiKey_LeftShift;
    case 17: return ImGuiKey_LeftCtrl;
    case 18: return ImGuiKey_LeftAlt;
    case 19: return ImGuiKey_Pause;
    case 27: return ImGuiKey_Escape;
    case 32: return ImGuiKey_Space;
    case 33: return ImGuiKey_PageUp;
    case 34: return ImGuiKey_PageDown;
    case 35: return ImGuiKey_End;
    case 36: return ImGuiKey_Home;
    case 37: return ImGuiKey_LeftArrow;
    case 38: return ImGuiKey_UpArrow;
    case 39: return ImGuiKey_RightArrow;
    case 40: return ImGuiKey_DownArrow;
    case 45: return ImGuiKey_Insert;
    case 46: return ImGuiKey_Delete;
    case 91: return ImGuiKey_LeftSuper;
    case 92: return ImGuiKey_RightSuper;
    case 186: return ImGuiKey_Semicolon;
    case 187: return ImGuiKey_Equal;
    case 188: return ImGuiKey_Comma;
    case 189: return ImGuiKey_Minus;
    case 190: return ImGuiKey_Period;
    case 191: return ImGuiKey_Slash;
    case 192: return ImGuiKey_GraveAccent;
    case 219: return ImGuiKey_LeftBracket;
    case 220: return ImGuiKey_Backslash;
    case 221: return ImGuiKey_RightBracket;
    case 222: return ImGuiKey_Apostrophe;
    default: return ImGuiKey_None;
  }
}

// Compares the live hair settings against a preset so the gallery can show
// which style is active even after the sliders were moved.
bool style_matches(const HairSettings& h, const HairstylePreset& p) {
  auto near = [](float a, float b) { return std::fabs(a - b) <= std::max(0.004f, std::fabs(b) * 0.04f); };
  return near(h.cut_length, p.cut_length) && near(h.curl_amount, p.curl_amount) &&
         near(h.clump_percent, p.clump_percent) && near(h.width_px, p.width_px) &&
         near(h.fuzz_amount, p.fuzz_amount) && near(h.volume, p.volume);
}

// --- lighting / weather presets, shared with the CLI names ------------------
struct ScenePreset {
  const char* name;
  const char* label;
  const char* icon;
  float sun_rot, sun_tilt, sun_int, exposure, bloom, clouds, turbidity, vignette;
  float rain, wetness, wind;
};
const ScenePreset kScenePresets[] = {
    {"studio", "Studio", ICON_FLASHLIGHT, 2.60f, -0.55f, 1.15f, 0.72f, 0.25f, 0.15f, 2.2f, 0.30f, 0.0f, 0.0f, 0.15f},
    {"noon", "Noon", ICON_WHITE_BALANCE_SUNNY, 2.60f, -1.05f, 1.15f, 0.62f, 0.20f, 0.20f, 2.0f, 0.30f, 0.0f, 0.0f, 0.15f},
    {"sunset", "Golden hour", ICON_SUN_THERMOMETER, 2.10f, -0.38f, 1.15f, 1.30f, 0.35f, 0.40f, 3.5f, 0.35f, 0.0f, 0.0f, 0.30f},
    {"backlit", "Backlit", ICON_BRIGHTNESS_6, 5.35f, -0.30f, 1.55f, 0.80f, 0.55f, 0.25f, 2.6f, 0.45f, 0.0f, 0.0f, 0.20f},
    {"overcast", "Overcast", ICON_WEATHER_PARTLY_CLOUDY, 2.60f, -0.75f, 0.70f, 0.95f, 0.20f, 0.85f, 4.5f, 0.30f, 0.0f, 0.0f, 0.35f},
    {"rain", "Rain", ICON_WEATHER_RAINY, 2.60f, -0.70f, 0.70f, 0.95f, 0.22f, 0.80f, 4.5f, 0.35f, 0.65f, 0.85f, 0.55f},
    {"storm", "Storm", ICON_UMBRELLA, 2.70f, -0.60f, 0.55f, 1.05f, 0.25f, 1.00f, 5.0f, 0.40f, 1.00f, 1.00f, 1.00f},
    {"dry", "Dry studio", ICON_HAIR_DRYER, 2.60f, -0.55f, 1.15f, 0.72f, 0.25f, 0.15f, 2.2f, 0.30f, 0.0f, 0.0f, 0.15f},
};

void apply_scene_preset(Scene& s, const ScenePreset& p) {
  s.sky.sun_rot = p.sun_rot;
  s.sky.sun_tilt = p.sun_tilt;
  s.sky.sun_intensity = p.sun_int;
  s.sky.exposure = p.exposure;
  s.sky.bloom = p.bloom;
  s.sky.cloud_coverage = p.clouds;
  s.sky.turbidity = p.turbidity;
  s.sky.vignette = p.vignette;
  s.weather.rain = p.rain;
  s.weather.wetness = p.wetness;
  s.weather.wind = p.wind;
  s.weather.lens_drops = p.rain > 0.0f ? p.rain * 0.8f : 0.0f;
}

// Colour chips for the dye palette. A ColorButton in a fixed grid keeps the
// whole 40-colour palette visible at once, which a list of names cannot.
bool dye_swatch(const DyeColor& c, bool active, float size) {
  ImGui::PushID(c.name);
  // 0xRRGGBB -> ImGui's packed ImU32 is RGBA with red in the low byte, so the
  // channels have to be reordered explicitly (a plain OR would swap red/blue).
  const ImU32 packed = IM_COL32((c.rgb >> 16) & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb & 0xFF, 255);
  const ImVec4 srgb = ImGui::ColorConvertU32ToFloat4(packed);
  const bool clicked = ImGui::ColorButton("##sw", srgb, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                          ImVec2(size, size));
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::ColorButton("##big", srgb, ImGuiColorEditFlags_NoTooltip, ImVec2(48, 48));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(c.name);
    ImGui::TextColored(kTextDim, "#%06X  %s", c.rgb, c.group);
    ImGui::EndGroup();
    ImGui::EndTooltip();
  }
  if (active) {
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), to_u32(ImVec4(1, 1, 1, 0.95f)),
                                        3.0f, 0, 2.0f);
  }
  ImGui::PopID();
  return clicked;
}

}  // namespace

// ---------------------------------------------------------------- lifecycle
UiPanel::UiPanel() = default;

UiPanel::~UiPanel() {
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  atlas_alpha_ = nullptr;
}

bool UiPanel::init(const std::string& fonts_dir, float scale) {
  scale_ = scale < 0.5f ? 0.5f : scale;
  fonts_dir_ = fonts_dir;
  IMGUI_CHECKVERSION();
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();  // re-init after --fonts
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // the layout lives in code, not in a stray .ini
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigDockingAlwaysTabBar = false;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  apply_style();
  gLabelWidth = 86.0f * scale_;
  build_fonts(scale_);
  atlas_alpha_ = nullptr;
  io.Fonts->GetTexDataAsAlpha8(&atlas_alpha_, &atlas_w_, &atlas_h_);
  io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
  ready_ = true;
  return true;
}

void UiPanel::build_fonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  std::string dir = fonts_dir_.empty() ? std::string("assets/fonts") : fonts_dir_;
  auto path = [&](const char* file) { return dir + "/" + file; };

  // One contiguous range per icon keeps the atlas small; ImGui builds the rest.
  // The array must outlive this call: ImFontAtlas keeps the pointer and reads it
  // when the atlas is built (GetTexDataAsAlpha8), not here.
  static std::vector<ImWchar> icon_ranges;
  if (icon_ranges.empty()) {
    for (int i = 0; i < kIconRangeCount; ++i) {
      icon_ranges.push_back(ImWchar(kIconRanges[i * 2]));
      icon_ranges.push_back(ImWchar(kIconRanges[i * 2 + 1]));
    }
    icon_ranges.push_back(0);
  }

  const float base = 15.0f * scale;
  const float small = 13.0f * scale;
  const float title = 19.0f * scale;

  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  cfg.PixelSnapH = false;
  font_regular_ = io.Fonts->AddFontFromFileTTF(path("Roboto-Regular.ttf").c_str(), base, &cfg);
  if (!font_regular_) {
    // No font files next to the binary: fall back to the built-in face so the
    // panel still works (it just loses the nicer type + the glyph icons).
    font_regular_ = io.Fonts->AddFontDefault();
    font_small_ = font_regular_;
    font_title_ = font_regular_;
    font_icon_ = nullptr;
    font_icon_big_ = nullptr;
    io.FontDefault = font_regular_;
    return;
  }
  ImFontConfig icon_cfg;
  icon_cfg.MergeMode = true;
  icon_cfg.PixelSnapH = true;
  icon_cfg.GlyphMinAdvanceX = base;
  io.Fonts->AddFontFromFileTTF(path("MaterialDesignIcons-Subset.ttf").c_str(), base, &icon_cfg, icon_ranges.data());

  font_small_ = io.Fonts->AddFontFromFileTTF(path("Roboto-Regular.ttf").c_str(), small, &cfg);
  ImFontConfig icon_cfg_small = icon_cfg;
  icon_cfg_small.GlyphMinAdvanceX = small;
  io.Fonts->AddFontFromFileTTF(path("MaterialDesignIcons-Subset.ttf").c_str(), small, &icon_cfg_small,
                               icon_ranges.data());

  ImFontConfig title_cfg;
  title_cfg.OversampleH = 2;
  title_cfg.OversampleV = 2;
  font_title_ = io.Fonts->AddFontFromFileTTF(path("Roboto-Medium.ttf").c_str(), title, &title_cfg);
  ImFontConfig icon_cfg_title = icon_cfg;
  icon_cfg_title.GlyphMinAdvanceX = title;
  io.Fonts->AddFontFromFileTTF(path("MaterialDesignIcons-Subset.ttf").c_str(), title, &icon_cfg_title,
                               icon_ranges.data());

  font_icon_ = font_regular_;
  font_icon_big_ = font_title_;
  io.FontDefault = font_regular_;
}

void UiPanel::apply_style() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();

  style.WindowRounding = 7.0f;
  style.ChildRounding = 5.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.GrabRounding = 4.0f;
  style.TabRounding = 5.0f;
  style.ScrollbarRounding = 6.0f;
  style.WindowPadding = ImVec2(11.0f, 10.0f);
  style.FramePadding = ImVec2(8.0f, 4.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
  style.IndentSpacing = 16.0f;
  style.ScrollbarSize = 11.0f;
  style.GrabMinSize = 11.0f;
  style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.SeparatorTextBorderSize = 1.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);

  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = ImVec4(0.42f, 0.45f, 0.50f, 1.00f);
  c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.059f, 0.071f, 0.97f);
  c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_PopupBg] = ImVec4(0.071f, 0.075f, 0.090f, 0.99f);
  c[ImGuiCol_Border] = ImVec4(0.24f, 0.25f, 0.29f, 0.55f);
  c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.23f, 1.00f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.24f, 0.29f, 1.00f);
  c[ImGuiCol_TitleBg] = ImVec4(0.071f, 0.075f, 0.090f, 1.00f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.10f, 0.13f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.07f, 0.90f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.055f, 0.059f, 0.071f, 1.00f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.60f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.27f, 0.32f, 0.85f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.35f, 0.41f, 0.95f);
  c[ImGuiCol_ScrollbarGrabActive] = kAccent;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.35f, 0.46f, 0.90f);
  c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.47f, 0.58f, 1.00f);
  c[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.19f, 0.24f, 1.00f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.22f, 0.29f, 1.00f);
  c[ImGuiCol_Header] = kAccentDim;
  c[ImGuiCol_HeaderHovered] = ImVec4(0.914f, 0.333f, 0.435f, 0.55f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.914f, 0.333f, 0.435f, 0.80f);
  c[ImGuiCol_Separator] = ImVec4(0.24f, 0.25f, 0.29f, 0.55f);
  c[ImGuiCol_SeparatorHovered] = kAccent;
  c[ImGuiCol_SeparatorActive] = kAccent;
  c[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.31f, 0.36f, 0.35f);
  c[ImGuiCol_ResizeGripHovered] = kAccentDim;
  c[ImGuiCol_ResizeGripActive] = kAccent;
  c[ImGuiCol_Tab] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
  c[ImGuiCol_TabHovered] = ImVec4(0.914f, 0.333f, 0.435f, 0.55f);
  c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.13f, 0.17f, 1.00f);
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_TabDimmed] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
  c[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.11f, 0.13f, 1.00f);
  c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.914f, 0.333f, 0.435f, 0.40f);
  c[ImGuiCol_DockingPreview] = kAccentDim;
  c[ImGuiCol_DockingEmptyBg] = ImVec4(0.03f, 0.03f, 0.04f, 1.00f);
  c[ImGuiCol_PlotLines] = kAccent;
  c[ImGuiCol_PlotHistogram] = kAccent;
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
  c[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.19f, 0.22f, 1.00f);
  c[ImGuiCol_TextSelectedBg] = kAccentDim;
  c[ImGuiCol_NavCursor] = kAccent;
}

// ---------------------------------------------------------------- input
void UiPanel::push_event(const UiInputEvent& e) {
  std::lock_guard<std::mutex> lock(pending_mutex_);
  // Mouse motion is coalesced: a slow software render can queue hundreds of
  // moves per frame and only the last one matters.
  if (e.type == UiInputEvent::MousePos && !pending_.empty() && pending_.back().type == UiInputEvent::MousePos) {
    pending_.back() = e;
    return;
  }
  if (pending_.size() > 512) pending_.erase(pending_.begin(), pending_.begin() + 128);
  pending_.push_back(e);
}

// ---------------------------------------------------------------- layout
void UiPanel::dock_layout(unsigned int dockspace_id) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::DockBuilderSetNodeSize(dockspace_id, vp->Size);

  // The parameter column gets relatively more room on a small frame: at 768 px
  // a 34 % column leaves the sliders too narrow to read their values.
  const float w = vp->Size.x;
  const float right_ratio = w >= 1400.0f ? 0.26f : (w >= 1100.0f ? 0.30f : (w >= 900.0f ? 0.35f : 0.43f));
  ImGuiID main = dockspace_id;
  ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, right_ratio, nullptr, &main);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.22f, nullptr, &main);
  ImGuiID right_low = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.52f, nullptr, &right);
  ImGuiID right_high = right;

  dock_right_ = right_high;
  dock_right_low_ = right_low;
  dock_bottom_ = bottom;
  fresh_layout_ = true;
  ImGui::DockBuilderDockWindow("Hair", right_high);
  ImGui::DockBuilderDockWindow("Colour & Dye", right_high);
  ImGui::DockBuilderDockWindow("Styles", right_high);
  ImGui::DockBuilderDockWindow("Lighting", right_low);
  ImGui::DockBuilderDockWindow("Ground", right_low);
  ImGui::DockBuilderDockWindow("Weather", right_low);
  ImGui::DockBuilderDockWindow("Camera", right_low);
  ImGui::DockBuilderDockWindow("Output", bottom);
  ImGui::DockBuilderDockWindow("Help", bottom);
  ImGui::DockBuilderFinish(dockspace_id);
}

// Docking is assigned twice on purpose: DockBuilder records it per window
// *settings*, which only covers windows that already exist; this also pushes it
// through the regular SetNextWindowDockID path, so the first frame after a
// layout (re)build docks everything. FirstUseEver afterwards, so the user's own
// arrangement is never fought with.
void UiPanel::dock_next(unsigned int node_id) {
  if (node_id == 0) return;
  ImGui::SetNextWindowDockID(node_id, fresh_layout_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
}

// ---------------------------------------------------------------- frame
UiResult UiPanel::frame(Scene& scene, const UiStats& stats, uint8_t* pixels, int w, int h, float dt) {
  UiResult result;
  if (!ready_) return result;

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(float(w), float(h));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
  if (!io.Fonts->IsBuilt() && atlas_alpha_) io.Fonts->Build();

  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (const UiInputEvent& e : pending_) {
      switch (e.type) {
        case UiInputEvent::MousePos: io.AddMousePosEvent(e.x, e.y); break;
        case UiInputEvent::MouseButton: io.AddMouseButtonEvent(e.button, e.down); break;
        case UiInputEvent::MouseWheel: io.AddMouseWheelEvent(0.0f, e.amount); break;
        case UiInputEvent::Text: io.AddInputCharactersUTF8(e.text.c_str()); break;
        case UiInputEvent::Key: {
          const ImGuiKey key = map_key(e.keycode);
          if (key != ImGuiKey_None) io.AddKeyEvent(key, e.down);
          break;
        }
      }
    }
    pending_.clear();
  }

  ImGui::NewFrame();


  // The dockspace *is* the render: passthrough central node, so the frame shows
  // through wherever no window sits.
  const ImGuiID dockspace_id = ImGui::GetID("HairGenDockspace");
  // DockBuilder calls must happen *before* the dockspace node is submitted for
  // the frame (that is what the ImGui docs require), and the layout is built on
  // the second frame so the windows already exist with a sensible size.
  if (first_frame_) {
    first_frame_ = false;
    build_layout_ = true;
  }
  if (build_layout_) {
    dock_layout(dockspace_id);
    build_layout_ = false;
    if (const char* dbg = getenv("HGEN_UI_DEBUG")) {
      (void)dbg;
      ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
      fprintf(stderr, "[ui] dockspace %u node=%p split=%d windows=%d\n", dockspace_id, (void*)node,
              node ? int(node->IsSplitNode()) : -1, node ? node->Windows.Size : -1);
    }
  }
  ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

  draw_frame(scene, stats, result);

  if (result.reset_layout) build_layout_ = true;  // rebuilt at the top of the next frame
  ImGui::Render();
  if (visible_) {
    ui_rasterize(ImGui::GetDrawData(), atlas_alpha_, atlas_w_, atlas_h_, pixels, w, h);
  }
  return result;
}

void UiPanel::draw_frame(Scene& scene, const UiStats& stats, UiResult& result) {
  draw_menu_bar(scene, stats, result);
  draw_viewport_overlay(stats);
  if (!visible_) return;

  // `solo_` renders a single window - that is what --ui-dump uses to photograph
  // one tab at a time (each tab would otherwise hide the others).
  auto want = [&](const char* id) { return solo_.empty() || solo_ == id; };
  if (want("hair")) draw_hair_window(scene, result);
  if (want("colour")) draw_colour_window(scene, result);
  if (want("styles")) draw_styles_window(scene, result);
  if (want("lighting")) draw_lighting_window(scene, result);
  if (want("ground")) draw_ground_window(scene, result);
  if (want("weather")) draw_weather_window(scene, result);
  if (want("camera")) draw_camera_window(scene, result);
  if (want("output")) draw_output_window(scene, stats, result);
  if (show_help_ || solo_ == "help") draw_help_window();
}

void UiPanel::draw_menu_bar(Scene& scene, const UiStats& stats, UiResult& result) {
  ImGui::PushFont(font_small_);
  if (ImGui::BeginMainMenuBar()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(ICON_HAIR_DRYER);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted("HairGen Studio");
    ImGui::SameLine(0.0f, 18.0f);

    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem(ICON_CONTENT_SAVE "  Save frame as PNG")) result.save_png = true;
      if (ImGui::MenuItem(ICON_IMAGE_OUTLINE "  Copy CLI command")) show_cli_ = true;
      ImGui::Separator();
      if (ImGui::MenuItem(ICON_CLOSE "  Quit")) result.quit = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Look")) {
      for (const ScenePreset& p : kScenePresets) {
        if (ImGui::MenuItem(p.label)) {
          apply_scene_preset(scene, p);
          result.changed = true;
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Front view")) {
        scene.camera.rot = 3.14159f;
        scene.camera.tilt = 0.0f;
        result.changed = true;
      }
      if (ImGui::MenuItem("Three-quarter view")) {
        scene.camera.rot = 3.05f;
        scene.camera.tilt = 0.02f;
        result.changed = true;
      }
      if (ImGui::MenuItem("Profile view")) {
        scene.camera.rot = 1.5708f;
        scene.camera.tilt = 0.02f;
        result.changed = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Panel")) {
      if (ImGui::MenuItem("Reset layout")) result.reset_layout = true;
      ImGui::MenuItem("Show statistics", nullptr, &show_stats_);
      ImGui::MenuItem("Show help", nullptr, &show_help_);
      ImGui::MenuItem("Show panel", nullptr, &visible_);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem(ICON_INFORMATION_OUTLINE "  About")) show_help_ = true;
      ImGui::EndMenu();
    }

    // Right aligned status: fps + the active style name.
    char status[160];
    const char* style = "custom";
    for (const HairstylePreset& p : kHairstyles)
      if (style_matches(scene.hair, p)) { style = p.label; break; }
    snprintf(status, sizeof(status), "%s  %s   %.1f fps   %u k tris", ICON_CHART_LINE, style,
             double(stats.fps), stats.triangles / 1000u);
    const float tw = ImGui::CalcTextSize(status).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - tw - 14.0f);
    ImGui::TextColored(kTextDim, "%s", status);
    ImGui::EndMainMenuBar();
  }
  ImGui::PopFont();
}

// A small always-on overlay in the corner of the viewport: the one thing you
// want while dragging sliders is the cost of the frame you are dragging.
void UiPanel::draw_viewport_overlay(const UiStats& stats) {
  if (!show_stats_) return;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12.0f, vp->WorkPos.y + 12.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.55f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##stats", nullptr, flags)) {
    ImGui::PushFont(font_small_);
    ImGui::TextColored(kAccent, "%s  %.1f fps", ICON_FLASHLIGHT, double(stats.fps));
    ImGui::TextColored(kTextDim, "%d x %d   %dx MSAA", stats.width, stats.height, stats.samples);
    ImGui::TextColored(kTextDim, "%u strands   %u k tris", stats.strands, stats.triangles / 1000u);
    ImGui::TextColored(kTextDim, ICON_COG "  %.1f ms cpu", stats.cpu_ms);
    ImGui::PopFont();
  }
  ImGui::End();
}

// ---------------------------------------------------------------- hair
void UiPanel::draw_hair_window(Scene& scene, UiResult& result) {
  HairSettings& h = scene.hair;
  dock_next(dock_right_);
  if (!ImGui::Begin(ICON_BRUSH "  Hair###hair")) {
    ImGui::End();
    return;
  }

  result.changed |= toggle("Hair", &h.show_hair);
  ImGui::SameLine();
  result.changed |= toggle("Head", &h.show_head);
  ImGui::SameLine();
  result.changed |= toggle("Floor", &scene.sky.show_ground);

  section(ICON_SHAPE_OUTLINE, "Silhouette");
  result.changed |= slider("Length", &h.cut_length, 0.15f, 1.0f, "%.2f",
                           "How much of each strand survives the cut.\n0.25 = buzz cut, 1 = the guide mesh's full length.", 1.0f);
  result.changed |= slider("Volume", &h.volume, 0.0f, 1.0f, "%.2f",
                           "Lifts the mass off the skull for big, round silhouettes.", 0.0f);
  result.changed |= slider("Width", &h.width_px, 1.0f, 8.0f, "%.2f px",
                           "Ribbon half-width in pixels: thin resolves single fibres, wide makes opaque clumps.", 3.2f);
  result.changed |= slider("Length var.", &h.length_var, 0.0f, 0.6f, "%.2f",
                           "Per-strand length jitter, which is what makes a hairline ragged.", 0.10f);
  result.changed |= slider_int("Strands", &h.hair_count, 2000, 60000,
                               "Strand count. Rebuilt on the fly (see the Output panel for the cost).");
  result.changed |= slider_int("Segments", &h.segments, 8, 64, "Segments per strand (geometry resolution).");

  section(ICON_TUNE, "Shading");
  result.changed |= slider("Density", &h.density, 0.0f, 8.0f, "%.2f",
                           "Shadow density: how much light the strand cloud absorbs (Beer-Lambert).", 2.5f);
  result.changed |= slider("Ambient", &h.ambient, 0.0f, 1.5f, "%.2f", "Sky light reaching the hair.", 0.30f);
  result.changed |= slider("Fill light", &h.fill_light, 0.0f, 1.0f, "%.2f",
                           "Cool fill from the shadow side (from mravella/hair), so the dark half keeps its shape.",
                           0.22f);
  result.changed |= slider("Sheen", &h.sheen, 0.0f, 1.0f, "%.2f",
                           "Anisotropic sheen: 0 = matte, 1 = freshly washed.", 0.35f);

  // ---- Unreal's Hair shading model, in Unreal's own vocabulary ----------
  section(ICON_CUBE_OUTLINE, "Hair model (UE)");
  result.changed |= slider("Scatter", &h.scatter, 0.0f, 1.0f, "%.2f",
                           "Unreal's Hair model \"Scatter\" input: how much of the light that enters the fibre\n"
                           "cloud scatters back out (dual scattering). UE's artistic range is 0.5-0.8;\n"
                           "0.65 is unity here.", 0.65f);
  result.changed |= slider("Specular", &h.specular, 0.0f, 1.5f, "%.2f",
                           "Unreal's \"Specular\" input: scale of the light-coloured highlight lobe.\n"
                           "UE artists usually keep this near 0.5.", 0.55f);
  float rough = h.roughness();
  if (slider("Roughness", &rough, 0.0f, 1.0f, "%.2f",
             "Unreal's \"Roughness\" input, remapped onto the longitudinal roughness of the\n"
             "R / TT / TRT fibre paths (0.4 reproduces the demo's material). UE suggests 0.3-0.5.\n"
             "Higher rougness widens the highlight instead of dimming it, which is what stops\n"
             "hair reading as plastic.", 0.40f)) {
    h.set_roughness(rough);
    result.changed = true;
  }
  result.changed |= slider("Backlit", &h.backlit, 0.0f, 0.5f, "%.3f",
                           "Unreal's \"Backlit\" input: light transmitted through strands lit from\n"
                           "behind. UE artists keep it small (0-0.1).", 0.06f);
  result.changed |= slider("Stylise", &h.stylise, 0.0f, 1.0f, "%.2f",
                           "How much of the demo's punchy lift / rim / sheen / saturation is kept.\n"
                           "Unreal's model has no such terms: 0 is the plain physical result,\n"
                           "1 is the original web demo.", 0.45f);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("Base Colour and Tangent are the dye and the per-strand tangent, so a UE groom "
                     "material ports over as these four numbers plus the dye.");
  ImGui::PopStyleColor();

  // ---- deep shadow -------------------------------------------------------
  section(ICON_LAYERS_TRIPLE, "Deep shadow");
  result.changed |= slider("Deep shadow", &h.deep_shadow, 0.0f, 1.0f, "%.2f",
                           "Unreal's Deep Shadow: absorption is bounded by the hair that is\n"
                           "actually on the light ray, measured by an extra additive pass in\n"
                           "light space. 0 falls back to the demo's single depth layer.", 0.65f);
  result.changed |= slider("Mass density", &h.deep_density, 0.05f, 2.0f, "%.2f",
                           "Scale of the measured hair density - how much a unit of covered\n"
                           "strand area counts as optical depth. Drives both the absorption\n"
                           "bound and the mass AO.", 1.0f);
  result.changed |= slider("Hair AO", &h.hair_ao, 0.0f, 1.0f, "%.2f",
                           "Ambient occlusion inside the mass from the same measured density:\n"
                           "strands buried under other strands see less sky and less fill.\n"
                           "Unreal takes this from the groom's voxelization.", 0.5f);

  // Open by default: this is the demo's own parameter set, and a panel that
  // hides the parameters the demo exposes is not a port of it.
  if (ImGui::CollapsingHeader(ICON_AUTO_FIX "  Operators", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped("The hair-mesh operator stack: each one is a displacement applied in the mesh's own "
                       "surface frame. The style gallery sets all of them at once.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    result.changed |= slider("Clump", &h.clump_percent, 0.0f, 1.0f, "%.2f");
    result.changed |= slider("Clump size", &h.clump_size, 0.0f, 0.3f, "%.3f");
    result.changed |= slider("Clump var", &h.clump_var, 0.0f, 1.0f, "%.2f");
    result.changed |= slider("Clump noise", &h.clump_noise, 0.0f, 3.0f, "%.2f");
    result.changed |= slider("Clump freq", &h.clump_freq, 0.1f, 4.0f, "%.2f");
    result.changed |= slider("Clump exp", &h.clump_exp, 0.0f, 4.0f, "%.2f");
    ImGui::Spacing();
    result.changed |= slider("Curl", &h.curl_amount, 0.0f, 0.4f, "%.3f");
    result.changed |= slider("Curl freq", &h.curl_freq, 0.1f, 4.0f, "%.2f");
    result.changed |= slider("Curl twists", &h.curl_twists, 0.0f, 30.0f, "%.1f");
    result.changed |= slider("Curl twist exp", &h.curl_twist_exp, 0.0f, 2.0f, "%.2f");
    result.changed |= slider("Curl exp", &h.curl_exp, 0.0f, 4.0f, "%.2f");
    ImGui::Spacing();
    result.changed |= slider("Kink", &h.kink_amount, 0.0f, 0.4f, "%.3f");
    result.changed |= slider("Kink freq UV", &h.kink_freq_uv, 0.1f, 8.0f, "%.2f");
    result.changed |= slider("Kink freq W", &h.kink_freq_w, 0.1f, 8.0f, "%.2f");
    result.changed |= slider("Kink W scale", &h.kink_w_scale, 0.0f, 2.0f, "%.2f");
    result.changed |= slider("Kink exp", &h.kink_exp, 0.0f, 4.0f, "%.2f");
    ImGui::Spacing();
    result.changed |= slider("Frizz", &h.frizz_amount, 0.0f, 0.4f, "%.3f");
    result.changed |= slider("Frizz freq", &h.frizz_freq, 0.1f, 8.0f, "%.2f");
    result.changed |= slider("Frizz exp", &h.frizz_exp, 0.0f, 4.0f, "%.2f");
    result.changed |= slider("Fuzz", &h.fuzz_amount, 0.0f, 0.4f, "%.3f");
    result.changed |= slider("Fuzz distrib", &h.fuzz_distrib, 0.0f, 3.0f, "%.2f");
    result.changed |= slider("Fuzz exp", &h.fuzz_exp, 0.0f, 4.0f, "%.2f");
  }

  if (ImGui::CollapsingHeader(ICON_LAYERS_TRIPLE "  Rendering")) {
    int mode = h.render_mode;
    result.changed |= ImGui::RadioButton("Ribbons", &mode, 1);
    ImGui::SameLine();
    result.changed |= ImGui::RadioButton("Lines", &mode, 0);
    h.render_mode = mode;
    result.changed |= toggle("Root samples", &h.use_root_samples, "Use the demo's explicit 1000-root table.");
    result.changed |= toggle("Head collision", &h.collide, "Push strands out of the head colliders.");
    result.changed |= toggle("Hair-mesh cage", &h.show_hairmesh, "Overlay the guide mesh.");
    result.changed |= toggle("Head casts shadow", &h.head_casts_shadow);

    // The fibre's absorption set. These are the demo's own material presets.
    ImGui::Spacing();
    ImGui::TextColored(kTextDim, "Fibre material");
    const char* mats[] = {"Std blond", "Rosa"};
    int mat = std::min<int>(h.material, 1);
    if (ImGui::Combo("##mat", &mat, mats, 2)) {
      h.material = mat;
      if (mat == 0) {
        h.a_f[0] = 0.507475266f; h.a_f[1] = 0.465571405f; h.a_f[2] = 0.394347166f;
      } else {
        h.a_f[0] = 0.68f; h.a_f[1] = 0.36f; h.a_f[2] = 0.42f;
      }
      result.changed = true;
    }
  }
  ImGui::End();
}

// ---------------------------------------------------------------- colour
void UiPanel::draw_colour_window(Scene& scene, UiResult& result) {
  dock_next(dock_right_);
  if (!ImGui::Begin(ICON_PALETTE "  Colour###colour")) {
    ImGui::End();
    return;
  }

  HairSettings& h = scene.hair;

  const float col_w = (avail() - 5.0f * 5.0f) / 6.0f;
  const float sw = std::max(18.0f, std::min(col_w, 34.0f));

  const unsigned current = rgb_from_linear(h.dye);
  if (ImGui::BeginTabBar("##dyegroups", ImGuiTabBarFlags_FittingPolicyScroll)) {
    if (ImGui::BeginTabItem("All")) {
      dye_group_ = -1;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Natural")) {
      dye_group_ = 0;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Fashion")) {
      dye_group_ = 1;
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  // A little air between the tab strip and the chips: they touch otherwise.
  ImGui::Spacing();

  int col = 0;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 5.0f));
  for (const DyeColor& c : kDyeColors) {
    const bool natural = std::strcmp(c.group, "Natural") == 0;
    if (dye_group_ == 0 && !natural) continue;
    if (dye_group_ == 1 && natural) continue;
    if (col++ % 6 != 0) ImGui::SameLine();
    if (dye_swatch(c, (c.rgb & 0xFFFFFF) == (current & 0xFFFFFF), sw)) {
      parse_hair_color(c.name, h.dye);
      if (h.dye_gradient <= 0.001f) {
        // A flat dye keeps root and tip identical, so the circle stays closed
        // until the user asks for a gradient.
        for (int i = 0; i < 3; ++i) h.dye_tip[i] = h.dye[i];
      }
      if (h.dye_strength < 0.05f) h.dye_strength = 0.88f;
      result.changed = true;
    }
  }
  ImGui::PopStyleVar();

  ImGui::Spacing();
  section(ICON_FORMAT_COLOR_FILL, "Root colour");
  ImVec4 root_srgb = vec4_from_linear(h.dye);
  if (ImGui::ColorEdit3("##root", &root_srgb.x, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayHex)) {
    linear_from_vec4(root_srgb, h.dye);
    if (h.dye_gradient <= 0.001f) for (int i = 0; i < 3; ++i) h.dye_tip[i] = h.dye[i];
    result.changed = true;
  }

  result.changed |= slider("Strength", &h.dye_strength, 0.0f, 1.0f, "%.2f",
                           "0 keeps the fibre's own colour, 1 is fully dyed.", 0.88f);

  section(ICON_GRADIENT_VERTICAL, "Ombre / tips");
  ImVec4 tip_srgb = vec4_from_linear(h.dye_tip);
  if (ImGui::ColorEdit3("##tip", &tip_srgb.x, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayHex)) {
    linear_from_vec4(tip_srgb, h.dye_tip);
    if (h.dye_gradient <= 0.001f) h.dye_gradient = 0.75f;
    result.changed = true;
  }
  result.changed |= slider("Gradient", &h.dye_gradient, 0.0f, 2.0f, "%.2f",
                           "0 = flat dye, 0.75 = soft ombre, 1.5 = dip dye (a hard band at the tips).", 0.0f);

  ImGui::Spacing();
  const ImVec2 bsz(std::max(46.0f, (avail() - 3.0f * 6.0f) / 4.0f), 0.0f);
  if (ImGui::Button("Flat", bsz)) {
    h.dye_gradient = 0.0f;
    for (int i = 0; i < 3; ++i) h.dye_tip[i] = h.dye[i];
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Ombre", bsz)) {
    const ImVec4 tip = shade(root_srgb, 0.22f);
    linear_from_vec4(tip, h.dye_tip);
    h.dye_gradient = 0.75f;
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Balayage", bsz)) {
    const ImVec4 tip = shade(root_srgb, 0.34f);
    linear_from_vec4(tip, h.dye_tip);
    h.dye_gradient = 1.0f;
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Dip dye", bsz)) {
    ImVec4 tip = root_srgb;
    ImGui::ColorConvertRGBtoHSV(tip.x, tip.y, tip.z, tip.x, tip.y, tip.z);
    const float hue = std::fmod(tip.x + 0.5f, 1.0f);
    ImGui::ColorConvertHSVtoRGB(hue, std::min(1.0f, tip.y * 1.15f + 0.15f), 0.85f, tip.x, tip.y, tip.z);
    linear_from_vec4(tip, h.dye_tip);
    h.dye_gradient = 1.6f;
    result.changed = true;
  }

  section(ICON_TUNE, "Variation");
  result.changed |= slider("Root ramp", &h.root_darken, 0.0f, 1.0f, "%.2f",
                           "Roots are denser and less exposed, so they read darker. 1.0 is mravella/hair's own \n"
                           "MIN_COLOR ramp (root = half the tip); 0 disables it.", 1.0f);
  result.changed |= slider("Per-strand", &h.color_variation, 0.0f, 0.8f, "%.2f",
                           "Per-strand tint jitter - the reason a mass of fibres reads as locks.", 0.22f);

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("Dyes tint the Marschner lookup tables' radiance: a dye can only subtract light, so the "
                     "colour is normalised by its brightest channel and the fibre keeps its own luminance "
                     "structure.");
  ImGui::PopStyleColor();

  ImGui::End();
}

// ---------------------------------------------------------------- styles
void UiPanel::draw_styles_window(Scene& scene, UiResult& result) {
  dock_next(dock_right_);
  if (!ImGui::Begin(ICON_SCISSORS_CUTTING "  Styles###styles")) {
    ImGui::End();
    return;
  }


  const char* filters[] = {"All", "Female", "Male", "Unisex"};
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::Combo("##filter", &style_filter_, filters, 4);
  ImGui::Spacing();

  const float card_w = std::max(74.0f, (avail() - 6.0f) / 2.0f);
  int shown = 0;
  for (const HairstylePreset& p : kHairstyles) {
    if (style_filter_ > 0 && std::strcmp(p.sex, filters[style_filter_]) != 0) continue;
    const bool active = style_matches(scene.hair, p);
    ImGui::PushID(p.name);
    if (shown % 2 != 0) ImGui::SameLine();
    const float alpha = active ? 0.55f : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.914f, 0.333f, 0.435f, 0.16f + alpha));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.914f, 0.333f, 0.435f, 0.42f));
    const char* sex_icon = std::strcmp(p.sex, "Male") == 0      ? ICON_FACE_MAN_PROFILE
                           : std::strcmp(p.sex, "Female") == 0 ? ICON_FACE_WOMAN_PROFILE
                                                               : ICON_ACCOUNT_GROUP;
    char card[96];
    snprintf(card, sizeof(card), "%s  %s", sex_icon, p.label);
    if (ImGui::Button(card, ImVec2(card_w, 34.0f))) {
      apply_hairstyle(scene.hair, p.name);
      result.changed = true;
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(kAccent, "%s  %s", p.label, ICON_FACE_MAN);
      ImGui::SameLine();
      ImGui::TextColored(kTextDim, "%s", p.sex);
      ImGui::Separator();
      ImGui::Text("length %.2f   volume %.2f   width %.1f", p.cut_length, p.volume, p.width_px);
      ImGui::Text("curl %.3f   kink %.3f   clump %.2f", p.curl_amount, p.kink_amount, p.clump_percent);
      ImGui::Text("fuzz %.3f   frizz %.3f   %d strands", p.fuzz_amount, p.frizz_amount, p.hair_count);
      ImGui::TextColored(kTextDim, "suggested dye: %s", p.dye_hint);
      ImGui::EndTooltip();
    }
    ImGui::PopID();
    ++shown;
  }
  if (shown == 0) hint("No styles in this group.");

  ImGui::Spacing();
  if (ImGui::Button(ICON_REFRESH "  Reapply active style", ImVec2(-FLT_MIN, 0.0f))) {
    for (const HairstylePreset& p : kHairstyles) {
      if (style_matches(scene.hair, p)) {
        apply_hairstyle(scene.hair, p.name);
        result.changed = true;
        break;
      }
    }
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-run the preset's operator stack, discarding manual slider edits.");

  ImGui::End();
}

// ---------------------------------------------------------------- lighting
void UiPanel::draw_lighting_window(Scene& scene, UiResult& result) {
  dock_next(dock_right_low_);
  if (!ImGui::Begin("  Lighting###lighting")) {
    ImGui::End();
    return;
  }

  SkySettings& s = scene.sky;

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
  const float bw = (avail() - 3.0f * 4.0f) / 4.0f;
  int n = 0;
  for (const ScenePreset& p : kScenePresets) {
    if (n % 4 != 0) ImGui::SameLine();
    ImGui::PushID(p.name);
    if (ImGui::Button(p.icon, ImVec2(bw, 28.0f))) {
      apply_scene_preset(scene, p);
      result.changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.label);
    ImGui::PopID();
    ++n;
  }
  ImGui::PopStyleVar();
  ImGui::Spacing();

  section(ICON_WHITE_BALANCE_SUNNY, "Sun");
  result.changed |= slider("Azimuth", &s.sun_rot, -3.15f, 3.15f, "%.2f", "Sun azimuth around the bust.");
  result.changed |= slider("Elevation", &s.sun_tilt, -1.45f, 0.0f, "%.2f", "0 = horizon, -1.57 = zenith.");
  result.changed |= slider("Intensity", &s.sun_intensity, 0.0f, 3.0f, "%.2f");
  result.changed |= slider("Exposure", &s.exposure, 0.1f, 2.5f, "%.2f");
  result.changed |= slider("Bloom", &s.bloom, 0.0f, 1.5f, "%.2f");
  result.changed |= slider("Vignette", &s.vignette, 0.0f, 1.0f, "%.2f");

  section(ICON_WEATHER_PARTLY_CLOUDY, "Sky (Preetham)");
  result.changed |= slider("Turbidity", &s.turbidity, 1.0f, 12.0f, "%.2f");
  result.changed |= slider("Rayleigh", &s.rayleigh, 0.0f, 3.0f, "%.2f");
  result.changed |= slider("Mie", &s.mie, 0.0f, 0.02f, "%.4f");
  result.changed |= slider("Mie G", &s.mie_g, 0.0f, 0.99f, "%.2f");
  result.changed |= slider("Clouds", &s.cloud_coverage, 0.0f, 1.0f, "%.2f");

  ImGui::End();
}

// ---------------------------------------------------------------- ground
void UiPanel::draw_ground_window(Scene& scene, UiResult& result) {
  dock_next(dock_right_low_);
  if (!ImGui::Begin("  Ground###ground")) {
    ImGui::End();
    return;
  }

  SkySettings& s = scene.sky;

  result.changed |= toggle("Show floor", &s.show_ground);
  result.changed |= slider("Height", &s.ground_y, -1.5f, 0.0f, "%.2f", "Floor height in world units (y).", -0.55f);

  section(ICON_GRID_LARGE, "Checkerboard");
  hint("Analytic, so it is filtered against the pixel footprint: the squares dissolve into a flat grey where "
       "they would otherwise alias, with no texture and no mip chain.");
  ImGui::Spacing();
  result.changed |= slider("Amount", &s.checker, 0.0f, 1.0f, "%.2f", "0 = plain stone, 1 = full checkerboard.", 1.0f);
  result.changed |= slider("Square size", &s.checker_scale, 0.25f, 8.0f, "%.2f",
                           "Squares per world unit: 1 = half-metre squares.", 1.6f);
  result.changed |= slider("Fade dist.", &s.checker_fade, 2.0f, 30.0f, "%.1f m",
                           "Distance at which the pattern has dissolved into the stone.", 7.0f);

  ImVec4 light = vec4_from_linear(s.checker_b);
  ImVec4 dark = vec4_from_linear(s.checker_a);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kTextDim, "Light square");
  ImGui::SameLine(gLabelWidth);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::ColorEdit3("##light", &light.x, ImGuiColorEditFlags_DisplayHex)) {
    linear_from_vec4(light, s.checker_b);
    result.changed = true;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kTextDim, "Dark square");
  ImGui::SameLine(gLabelWidth);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::ColorEdit3("##dark", &dark.x, ImGuiColorEditFlags_DisplayHex)) {
    linear_from_vec4(dark, s.checker_a);
    result.changed = true;
  }
  if (ImGui::Button("Classic grey", ImVec2((avail() - 4.0f) * 0.5f, 0.0f))) {
    s.checker_a[0] = s.checker_a[1] = 0.196f; s.checker_a[2] = 0.202f;
    s.checker_b[0] = s.checker_b[1] = 0.585f; s.checker_b[2] = 0.590f;
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Studio white", ImVec2(-FLT_MIN, 0.0f))) {
    s.checker_a[0] = s.checker_a[1] = s.checker_a[2] = 0.32f;
    s.checker_b[0] = s.checker_b[1] = s.checker_b[2] = 0.78f;
    result.changed = true;
  }

  section(ICON_WATER_PERCENT, "Water");
  result.changed |= slider("Puddles", &scene.weather.puddles, 0.0f, 1.0f, "%.2f");
  result.changed |= slider("Wetness", &scene.weather.wetness, 0.0f, 1.0f, "%.2f",
                           "0 = follow the rain amount, otherwise an explicit surface wetness.", 0.0f);

  ImGui::End();
}

// ---------------------------------------------------------------- weather
void UiPanel::draw_weather_window(Scene& scene, UiResult& result) {
  dock_next(dock_right_low_);
  if (!ImGui::Begin("  Weather###weather")) {
    ImGui::End();
    return;
  }

  WeatherSettings& w = scene.weather;

  result.changed |= slider("Rain", &w.rain, 0.0f, 1.0f, "%.2f",
                           "Rain streaks, veil, wet skin/hair and puddle ripples all follow this.", 0.0f);
  result.changed |= slider("Wetness", &w.wetness, 0.0f, 1.0f, "%.2f", "Surface wetness (0 = tracks the rain).", 0.0f);
  result.changed |= slider("Wind", &w.wind, 0.0f, 2.0f, "%.2f", "Bends the hair and shears the rain.", 0.0f);
  result.changed |= slider("Wind dir.", &w.wind_dir, -3.15f, 3.15f, "%.2f", "Wind azimuth (rad).", 0.7f);
  result.changed |= slider("Gravity", &w.gravity, 0.0f, 1.5f, "%.2f", "How much the free length sags.", 0.35f);
  result.changed |= slider("Lens drops", &w.lens_drops, 0.0f, 1.0f, "%.2f", "Droplets on the virtual lens.", 0.0f);
  result.changed |= slider("Speed", &w.speed, 0.0f, 3.0f, "%.2f", "Animation speed of every effect.", 1.0f);

  ImGui::End();
}

// ---------------------------------------------------------------- camera
void UiPanel::draw_camera_window(Scene& scene, UiResult& result) {
  dock_next(dock_right_low_);
  if (!ImGui::Begin("  Camera###camera")) {
    ImGui::End();
    return;
  }

  CameraSettings& c = scene.camera;

  result.changed |= slider("Orbit", &c.rot, -3.20f, 3.20f, "%.2f", "Orbit angle around the bust (rad).", 3.05f);
  result.changed |= slider("Tilt", &c.tilt, -0.6f, 0.6f, "%.2f", "Elevation (positive looks down).", 0.02f);
  result.changed |= slider("Distance", &c.dist, 0.8f, 6.0f, "%.2f", "Orbit radius in metres.", 2.6f);
  result.changed |= slider("Field of view", &c.fov, 15.0f, 90.0f, "%.1f deg", "Vertical field of view.", 45.0f);

  ImGui::Spacing();
  const float bw = (avail() - 3.0f * 4.0f) / 4.0f;
  if (ImGui::Button("Front", ImVec2(bw, 0.0f))) {
    c.rot = 3.14159f;
    c.tilt = 0.0f;
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("3/4", ImVec2(bw, 0.0f))) {
    c.rot = 3.05f;
    c.tilt = 0.02f;
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Side", ImVec2(bw, 0.0f))) {
    c.rot = 1.5708f;
    c.tilt = 0.02f;
    result.changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Back", ImVec2(-FLT_MIN, 0.0f))) {
    c.rot = 0.0f;
    c.tilt = 0.02f;
    result.changed = true;
  }
  if (ImGui::Button(ICON_REFRESH "  Reset camera", ImVec2(-FLT_MIN, 0.0f))) {
    c = CameraSettings{};
    result.changed = true;
  }

  ImGui::End();
}

// ---------------------------------------------------------------- output
void UiPanel::draw_output_window(Scene& scene, const UiStats& stats, UiResult& result) {
  dock_next(dock_bottom_);
  if (!ImGui::Begin(ICON_CONTENT_SAVE "  Output###output")) {
    ImGui::End();
    return;
  }


  ImGui::Columns(2, "##out", false);
  ImGui::TextColored(kTextDim, "frame");
  ImGui::TextColored(kTextDim, "msaa");
  ImGui::TextColored(kTextDim, "triangles");
  ImGui::TextColored(kTextDim, "strands");
  ImGui::NextColumn();
  ImGui::Text("%d x %d", stats.width, stats.height);
  ImGui::Text("%dx", stats.samples);
  ImGui::Text("%u", stats.triangles);
  ImGui::Text("%u", stats.strands);
  ImGui::Columns(1);
  ImGui::Separator();

  if (ImGui::Button(ICON_CONTENT_SAVE "  Save frame", ImVec2(-FLT_MIN, 30.0f))) result.save_png = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes hairgen_frame.png next to the binary (and in the repo root).");

  if (ImGui::Button(show_cli_ ? ICON_CLOSE "  Hide CLI command" : ICON_IMAGE_OUTLINE "  Show CLI command",
                    ImVec2(-FLT_MIN, 0.0f))) {
    show_cli_ = !show_cli_;
    if (show_cli_) {
      snprintf(cli_buf_, sizeof(cli_buf_), "%s", cli_command(scene).c_str());
    }
  }
  if (show_cli_) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped("Reproduces this exact look from the shell:");
    ImGui::PopStyleColor();
    ImGui::InputTextMultiline("##cli", cli_buf_, sizeof(cli_buf_), ImVec2(-FLT_MIN, 76.0f),
                              ImGuiInputTextFlags_ReadOnly);
  }

  if (ImGui::Button(ICON_REFRESH "  Reset everything", ImVec2(-FLT_MIN, 0.0f))) {
    const CameraSettings cam = scene.camera;
    scene = Scene{};
    scene.camera = cam;
    result.changed = true;
  }

  ImGui::End();
}

// ---------------------------------------------------------------- help
void UiPanel::draw_help_window() {
  dock_next(dock_bottom_);
  if (!ImGui::Begin(ICON_INFORMATION_OUTLINE "  About the panel###help")) {
    ImGui::End();
    return;
  }

  hint("Drag a window's title bar to dock or float it. Every parameter group is a normal ImGui window: split "
       "the dock, tab two of them together, or close what you do not need (Panel > Reset layout brings the "
       "defaults back).");
  ImGui::Spacing();
  hint("Sliders: drag to scrub, Ctrl+click to type a value, right-click to reset to the default.");
  ImGui::Spacing();
  hint("The panel is drawn on the CPU and composited over the GPU frame, so it works on a headless machine - "
       "there is no window, no swapchain and no second graphics API.");

  ImGui::End();
}

// ---------------------------------------------------------------- CLI echo
// Reproduces the current look from the shell. Only values that differ from the
// defaults are emitted, so the string stays readable - and it is the same
// vocabulary `--help` documents, which makes it useful as a bug report too.
std::string UiPanel::cli_command(const Scene& scene) {
  std::string out = "hairgen --out render.png --size 768";
  char buf[256];
  auto push = [&](const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    out += " ";
    out += buf;
  };
  auto f3 = [](float v) { return double(v); };

  const HairSettings& h = scene.hair;
  for (const HairstylePreset& p : kHairstyles) {
    if (style_matches(h, p)) {
      push("--style %s", p.name);
      break;
    }
  }
  // dye: prefer a palette name, otherwise the hex
  unsigned rgb = rgb_from_linear(h.dye);
  const char* dye_name = nullptr;
  for (const DyeColor& c : kDyeColors)
    if ((c.rgb & 0xFFFFFF) == (rgb & 0xFFFFFF)) { dye_name = c.name; break; }
  if (dye_name) push("--dye %s", dye_name);
  else push("--dye #%06X", rgb & 0xFFFFFF);
  if (h.dye_strength < 0.999f) push("--dye-strength %.2f", f3(h.dye_strength));
  if (h.dye_gradient > 0.001f) {
    push("--dye-gradient %.2f", f3(h.dye_gradient));
    const unsigned tip = rgb_from_linear(h.dye_tip);
    const char* tip_name = nullptr;
    for (const DyeColor& c : kDyeColors)
      if ((c.rgb & 0xFFFFFF) == (tip & 0xFFFFFF)) { tip_name = c.name; break; }
    if (tip_name) push("--dye-tip %s", tip_name);
    else push("--dye-tip #%06X", tip & 0xFFFFFF);
  }
  if (h.cut_length < 0.999f) push("--cut-length %.2f", f3(h.cut_length));
  if (h.volume > 0.001f) push("--volume %.2f", f3(h.volume));
  if (h.width_px != HairSettings{}.width_px) push("--width-px %.2f", f3(h.width_px));
  if (h.hair_count != HairSettings{}.hair_count) push("--hair-count %d", h.hair_count);
  if (h.clump_size > 0.001f) push("--clump-size %.3f", f3(h.clump_size));
  if (h.fill_light != HairSettings{}.fill_light) push("--fill %.2f", f3(h.fill_light));
  if (h.sheen != HairSettings{}.sheen) push("--sheen %.2f", f3(h.sheen));
  if (h.deep_shadow != HairSettings{}.deep_shadow) push("--deep-shadow %.2f", f3(h.deep_shadow));
  if (h.deep_density != HairSettings{}.deep_density) push("--deep-density %.2f", f3(h.deep_density));
  if (h.hair_ao != HairSettings{}.hair_ao) push("--hair-ao %.2f", f3(h.hair_ao));
  if (h.scatter != HairSettings{}.scatter) push("--scatter %.2f", f3(h.scatter));
  if (h.specular != HairSettings{}.specular) push("--specular %.2f", f3(h.specular));
  if (h.backlit != HairSettings{}.backlit) push("--backlit %.3f", f3(h.backlit));
  if (h.stylise != HairSettings{}.stylise) push("--stylise %.2f", f3(h.stylise));
  if (h.material == 0) push("--material std_blond");
  if (h.render_mode == 0) push("--lines");
  if (!h.collide) push("--no-collide");
  if (h.use_root_samples) push("--root-samples");
  if (h.show_hairmesh) push("--show-hairmesh");

  const SkySettings& sk = scene.sky;
  push("--rot %.2f --tilt %.2f --dist %.2f", f3(scene.camera.rot), f3(scene.camera.tilt), f3(scene.camera.dist));
  push("--sun-rot %.2f --sun-tilt %.2f --sun-intensity %.2f", f3(sk.sun_rot), f3(sk.sun_tilt),
       f3(sk.sun_intensity));
  push("--exposure %.2f --bloom %.2f --clouds %.2f", f3(sk.exposure), f3(sk.bloom), f3(sk.cloud_coverage));
  push("--turbidity %.2f --rayleigh %.2f --mie %.4f", f3(sk.turbidity), f3(sk.rayleigh), f3(sk.mie));
  if (sk.checker < 0.999f) push("--checker %.2f", f3(sk.checker));
  if (sk.checker_scale != SkySettings{}.checker_scale) push("--checker-scale %.2f", f3(sk.checker_scale));
  const WeatherSettings& w = scene.weather;
  if (w.rain > 0.001f) push("--rain %.2f --wetness %.2f --wind %.2f", f3(w.rain), f3(w.wetness), f3(w.wind));
  return out;
}

}  // namespace hgen
