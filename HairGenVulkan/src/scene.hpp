// scene.hpp - user facing parameters (shared by the CLI, the web UI and the
// capture tool).
#pragma once

#include <string>

#include "math.hpp"

namespace hgen {

struct CameraSettings {
  float rot = 2.5f;      // orbit angle (rad)
  float tilt = -0.06f;   // elevation (rad, positive looks down)
  float dist = 2.55f;    // orbit radius
  float fov = 45.0f;     // vertical field of view (deg)
  vec3 target{0.02f, -0.02f, 0.0f};
  float focusY = -1.02f; // ground height in world units
};

// Styling parameters. Defaults match the reference demo's initial state, the
// ranges match its sliders.
struct HairSettings {
  bool show_hair = true;
  bool show_head = true;

  float density = 1.0f;       // shadow density ("shadows" slider)
  float length_var = 0.1f;

  float fuzz_amount = 0.10f, fuzz_distrib = 0.80f, fuzz_exp = 0.0f;
  float frizz_amount = 0.02f, frizz_freq = 1.0f, frizz_exp = 0.0f;
  float kink_amount = 0.02f, kink_freq_uv = 2.0f, kink_freq_w = 1.0f, kink_w_scale = 0.5f, kink_exp = 0.0f;
  float clump_percent = 0.90f, clump_size = 0.0f, clump_noise = 1.0f, clump_freq = 1.0f, clump_var = 0.1f,
        clump_exp = 0.0f;
  float curl_amount = 0.02f, curl_freq = 1.0f, curl_twists = 10.0f, curl_twist_exp = 0.3f, curl_exp = 0.0f;

  float width_px = 1.6f;   // ribbon width in pixels
  int hair_count = 10000;
  int segments = 32;

  // Fiber (Marschner) parameters - the demo's material LUTs are tuned for these.
  float a_f[3] = {0.507475266f, 0.465571405f, 0.394347166f};
  float r_dev = 8.0f, tt_dev = 10.0f, trt_dev = 15.0f;
  float ambient = 0.35f;
};

struct SkySettings {
  // Defaults chosen so the key light matches the reference demo's view-space
  // light (upper left, slightly in front) for the default camera.
  float sun_rot = 2.02f;     // sun azimuth (rad, world)
  float sun_tilt = -0.865f;  // sun elevation (rad, negative = above)
  float sun_intensity = 1.15f;
  float exposure = 1.0f;
  float bloom = 0.22f;
  float cloud_coverage = 0.35f;
  float vignette = 0.28f;
  bool show_ground = true;
  float ground_y = -1.02f;
  bool show_hair_mesh = false;
};

struct RenderOptions {
  int width = 768;
  int height = 768;
  int shadow_res = 1024;
  int samples = 4;   // MSAA
  std::string output_png;
  int frames = 1;
  std::string sequence_dir;
  std::string animation = "none";  // none | orbit | sweep | light
  float animation_speed = 1.0f;
  float fps = 30.0f;
  bool bloom = true;
  bool write_json = false;
};

struct Scene {
  CameraSettings camera;
  HairSettings hair;
  SkySettings sky;
};

}  // namespace hgen
