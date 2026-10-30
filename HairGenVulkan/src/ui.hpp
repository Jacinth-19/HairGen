// ui.hpp - the dockable Dear ImGui panel.
//
// The renderer has no window: it renders into an offscreen HDR target, reads the
// frame back and streams it to a browser. The panel is built for exactly that
// situation - Dear ImGui runs without an OS backend (input is pushed in from the
// HTTP server, output is rasterised on the CPU by ui_draw.cpp and composited
// over the frame), so the whole studio UI is available on a headless machine.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "scene.hpp"

struct ImFont;

namespace hgen {

struct RenderStats;

// One queued input event. The panel is driven from a different thread than the
// renderer, so events are buffered and drained at the top of a frame (which is
// also how ImGui wants them: they must be submitted before NewFrame()).
struct UiInputEvent {
  enum Type { MousePos, MouseButton, MouseWheel, Key, Text } type = MousePos;
  float x = 0.0f;
  float y = 0.0f;
  float amount = 0.0f;
  int button = 0;
  bool down = false;
  // Browser keyCode (legacy VK): mapped to ImGuiKey inside the panel.
  int keycode = 0;
  std::string text;
};

struct UiStats {
  int width = 0;
  int height = 0;
  int samples = 4;
  float fps = 0.0f;
  double cpu_ms = 0.0;
  double gpu_ms = 0.0;
  unsigned triangles = 0;
  unsigned strands = 0;
  int hairstyle = -1;
};

struct UiResult {
  bool changed = false;      // the scene was edited this frame
  bool save_png = false;     // the user pressed "save frame"
  bool reset_layout = false; // rebuild the dock layout
  bool quit = false;
};

class UiPanel {
 public:
  UiPanel();
  ~UiPanel();

  // `fonts_dir` holds Roboto-*.ttf and MaterialDesignIcons-Subset.ttf; when the
  // files are missing the built-in ImGui font is used so the panel still works.
  bool init(const std::string& fonts_dir, float scale);
  bool ready() const { return ready_; }
  void build_fonts(float scale);

  // Thread safe: called from the HTTP server threads.
  void push_event(const UiInputEvent& e);

  // Builds one frame and composites it over `pixels` (RGBA8, `w * h`, top-down).
  UiResult frame(Scene& scene, const UiStats& stats, uint8_t* pixels, int w, int h, float dt);

  // Renders only one window ("" = all). Used by --ui-dump.
  void set_solo(const std::string& id) { solo_ = id; }

  void toggle_visible() { visible_ = !visible_; }
  bool visible() const { return visible_; }
  void set_visible(bool v) { visible_ = v; }

  // The exact command line for the current scene, shown in the panel so a look
  // can be reproduced from the shell (or pasted into a bug report).
  static std::string cli_command(const Scene& scene);

 private:
  void apply_style();
  void dock_layout(unsigned int dockspace_id);
  void draw_frame(Scene& scene, const UiStats& stats, UiResult& result);
  void draw_menu_bar(Scene& scene, const UiStats& stats, UiResult& result);
  void draw_viewport_overlay(const UiStats& stats);
  void draw_hair_window(Scene& scene, UiResult& result);
  void draw_colour_window(Scene& scene, UiResult& result);
  void draw_styles_window(Scene& scene, UiResult& result);
  void draw_lighting_window(Scene& scene, UiResult& result);
  void draw_ground_window(Scene& scene, UiResult& result);
  void draw_weather_window(Scene& scene, UiResult& result);
  void draw_camera_window(Scene& scene, UiResult& result);
  void draw_output_window(Scene& scene, const UiStats& stats, UiResult& result);
  void draw_help_window();

  // ---- state
  bool ready_ = false;
  bool visible_ = true;
  bool first_frame_ = true;
  bool build_layout_ = false;
  bool fresh_layout_ = true;
  // Dock node ids from the last build (0 until the layout has been built).
  unsigned int dock_right_ = 0;
  unsigned int dock_right_low_ = 0;
  unsigned int dock_bottom_ = 0;
  void dock_next(unsigned int node_id);
  float scale_ = 1.0f;
  std::string fonts_dir_;
  std::string solo_;
  std::vector<UiInputEvent> pending_;
  std::mutex pending_mutex_;

  unsigned char* atlas_alpha_ = nullptr;
  int atlas_w_ = 0;
  int atlas_h_ = 0;

  ImFont* font_regular_ = nullptr;
  ImFont* font_small_ = nullptr;
  ImFont* font_title_ = nullptr;
  ImFont* font_icon_ = nullptr;
  ImFont* font_icon_big_ = nullptr;

  // panel-only state (never part of the scene)
  int style_filter_ = 0;         // 0 all, 1 female, 2 male, 3 unisex
  bool show_help_ = false;
  bool show_stats_ = true;
  bool show_cli_ = false;
  char cli_buf_[1024] = {0};
  char hex_buf_[16] = "#E8556F";
  float ombre_backup_[3] = {0.0f, 0.0f, 0.0f};
  int dye_group_ = 0;
  int last_dye_edit_ = 0;
};

}  // namespace hgen
