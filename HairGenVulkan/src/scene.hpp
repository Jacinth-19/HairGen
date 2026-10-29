// scene.hpp - user facing parameters (shared by the CLI, the web UI and the
// capture tool).
#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "math.hpp"

namespace hgen {

struct CameraSettings {
  float rot = 3.05f;     // orbit angle (rad) - three-quarter portrait view
  float tilt = 0.02f;    // elevation (rad, positive looks down)
  float dist = 2.6f;     // orbit radius
  float fov = 45.0f;     // vertical field of view (deg)
  vec3 target{0.02f, 0.02f, 0.0f};
  float focusY = -0.55f; // ground height in world units
};

// Styling parameters. Defaults match the reference demo's initial state, the
// ranges match its sliders.
struct HairSettings {
  bool show_hair = true;
  bool show_head = true;
  bool show_hairmesh = false;  // "show hair mesh": draw the control cage (demo toggle)
  int render_mode = 1;         // 0 = lines (the demo's default), 1 = ribbons
  bool use_root_samples = false;  // roots from july_rootsamples.js instead of procedural
  bool collide = true;
  // The bust is a torn, double-sided strip: rendered into the shadow map it
  // shadows itself along polygon edges, which is what produced the hard
  // faceted patches on the forehead. The hair's shadow (a separate map) is the
  // one that matters, so by default the head does not cast.
  bool head_casts_shadow = false;            // push strands out of the july_colliders.js spheres

  float density = 1.0f;       // shadow density ("shadows" slider)
  float length_var = 0.1f;

  float fuzz_amount = 0.10f, fuzz_distrib = 0.80f, fuzz_exp = 0.0f;
  float frizz_amount = 0.02f, frizz_freq = 1.0f, frizz_exp = 0.0f;
  float kink_amount = 0.02f, kink_freq_uv = 2.0f, kink_freq_w = 1.0f, kink_w_scale = 0.5f, kink_exp = 0.0f;
  float clump_percent = 0.90f, clump_size = 0.0f, clump_noise = 1.0f, clump_freq = 1.0f, clump_var = 0.1f,
        clump_exp = 0.0f;
  float curl_amount = 0.02f, curl_freq = 1.0f, curl_twists = 10.0f, curl_twist_exp = 0.3f, curl_exp = 0.0f;

  float width_px = 1.2f;   // ribbon width in pixels
  int hair_count = 20000;
  int segments = 32;

  // Fiber (Marschner) parameters - the demo's material LUTs are tuned for these.
  float a_f[3] = {0.507475266f, 0.465571405f, 0.394347166f};
  float r_dev = 8.0f, tt_dev = 10.0f, trt_dev = 15.0f;
  float ambient = 0.20f;

  // Index into the asset bundle's material list (0 = std_blond, 1 = rosa).
  // Selects the Marschner lookup tables *and* the absorption preset.
  int material = 0;

  // ---- hair dye -------------------------------------------------------
  // The LUTs bake one fibre's absorption, so recolouring them means tinting the
  // scattered radiance. `dye` is the target colour (linear), applied at
  // `dye_strength` while keeping the luminance structure the LUTs provide.
  float dye[3] = {0.356f, 0.062f, 0.115f};  // rosa: #E8556F in linear
  float dye_strength = 0.88f;
  float root_darken = 0.55f;    // roots are denser and less exposed -> darker
  float color_variation = 0.22f;  // per-strand tint jitter (mravella/hair)
  float sheen = 0.35f;          // extra sky-facing sheen
};


// ---- hair dye colour parsing -------------------------------------------
// sRGB hex (#RRGGBB) or a named colour -> linear RGB. The renderer shades in
// linear radiance, so the conversion happens once, here.
struct DyeColor { const char* name; unsigned rgb; };
inline const DyeColor kDyeColors[] = {
    {"rosa", 0xE8556F},    {"pink", 0xF26A85},     {"rose", 0xE0596E},
    {"platinum", 0xEDE4D6},{"blonde", 0xD9B978},   {"blond", 0xD9B978},
    {"copper", 0xC0562A},  {"auburn", 0x7E3018},   {"burgundy", 0x5C1226},
    {"black", 0x161214},   {"brown", 0x4A2C1C},    {"chestnut", 0x6B3A22},
    {"violet", 0x7B4BCB},  {"teal", 0x2C8C8C},     {"silver", 0xC6C8CE},
    {"sunset", 0xFF7A3E},  {"mint", 0x8FE0C0},     {"lilac", 0xC7A9F0},
    {"raven", 0x241E28},   {"wine", 0x8E1F3A},
};

inline bool parse_hair_color(const std::string& in, float out[3]) {
  unsigned rgb = 0;
  if (!in.empty() && in[0] == '#') {
    char* end = nullptr;
    rgb = unsigned(std::strtoul(in.c_str() + 1, &end, 16));
    if (end == in.c_str() + 1) return false;
  } else {
    bool found = false;
    for (const DyeColor& c : kDyeColors)
      if (in == c.name) { rgb = c.rgb; found = true; break; }
    if (!found) return false;
  }
  for (int c = 0; c < 3; ++c) {
    const float srgb = float((rgb >> (16 - 8 * c)) & 0xff) / 255.0f;
    out[c] = srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
  }
  return true;
}

// ---- hairstyle presets -------------------------------------------------
// The demo only ships one hair mesh; the "styles" here are procedural: the same
// roots and the same strip, restyled by the fuzz / frizz / kink / clump / curl
// operator stack (the technique the SIGGRAPH'24 hair-mesh paper uses to author
// many styles from one guide mesh). Each preset is a full set of operator
// values, so switching styles is one call.
struct HairstylePreset {
  const char* name;
  float curl_amount, curl_freq, curl_twists;
  float kink_amount, kink_freq_uv;
  float clump_percent, clump_size, clump_var;
  float fuzz_amount, frizz_amount;
  float length_var, width_px;
  int hair_count;
};

inline const HairstylePreset kHairstyles[] = {
    // The strip's silhouette is fixed by the guide mesh, so the presets
    // differentiate themselves in *texture*: curl/kink amount, clump structure,
    // fuzz (stray fibres) and ribbon width (how much of the mass each strand
    // covers). Values are tuned against the renderer, not guessed.
    {"bob",    0.02f, 1.0f, 10.0f, 0.020f, 2.0f, 0.90f, 0.000f, 0.10f, 0.100f, 0.020f, 0.10f, 1.4f, 20000},
    {"sleek",  0.03f, 1.2f,  9.0f, 0.010f, 1.4f, 0.96f, 0.000f, 0.04f, 0.030f, 0.008f, 0.05f, 1.3f, 26000},
    {"wavy",   0.05f, 1.5f, 12.0f, 0.030f, 2.4f, 0.86f, 0.008f, 0.20f, 0.080f, 0.030f, 0.14f, 1.7f, 22000},
    {"curly",  0.16f, 2.0f, 16.0f, 0.140f, 3.2f, 0.78f, 0.016f, 0.35f, 0.300f, 0.060f, 0.20f, 2.4f, 24000},
    {"afro",   0.18f, 2.6f, 20.0f, 0.180f, 3.8f, 0.55f, 0.028f, 0.50f, 0.420f, 0.120f, 0.26f, 2.8f, 26000},
    {"pixie",  0.09f, 1.8f, 14.0f, 0.070f, 2.8f, 0.82f, 0.010f, 0.28f, 0.180f, 0.045f, 0.30f, 2.0f, 22000},
    {"long",   0.04f, 1.3f, 11.0f, 0.018f, 1.8f, 0.93f, 0.000f, 0.06f, 0.060f, 0.014f, 0.06f, 1.3f, 20000},
    {"punk",   0.12f, 3.0f, 24.0f, 0.200f, 4.2f, 0.50f, 0.040f, 0.55f, 0.240f, 0.200f, 0.24f, 2.2f, 24000},
};

inline bool apply_hairstyle(struct HairSettings& h, const std::string& name) {
  for (const HairstylePreset& p : kHairstyles) {
    if (name != p.name) continue;
    h.curl_amount = p.curl_amount;
    h.curl_freq = p.curl_freq;
    h.curl_twists = p.curl_twists;
    h.kink_amount = p.kink_amount;
    h.kink_freq_uv = p.kink_freq_uv;
    h.clump_percent = p.clump_percent;
    h.clump_size = p.clump_size;
    h.clump_var = p.clump_var;
    h.fuzz_amount = p.fuzz_amount;
    h.frizz_amount = p.frizz_amount;
    h.length_var = p.length_var;
    h.width_px = p.width_px;
    h.hair_count = p.hair_count;
    return true;
  }
  return false;
}

struct SkySettings {
  // Defaults chosen so the key light matches the reference demo's view-space
  // light (upper left, slightly in front) for the default camera.
  float sun_rot = 1.35f;     // sun azimuth (rad, world) - frontal key
  float sun_tilt = -0.55f;   // sun elevation (rad, negative = above)
  float sun_intensity = 1.15f;
  float exposure = 1.0f;
  float bloom = 0.25f;
  float cloud_coverage = 0.15f;
  float vignette = 0.30f;
  bool show_ground = true;
  float ground_y = -0.55f;
  bool show_hair_mesh = false;
};

struct RenderOptions {
  int width = 768;
  int height = 768;
  int shadow_res = 1024;
  int samples = 4;   // MSAA
  int supersample = 1;  // render at N x resolution and box-filter down (still frames)
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
