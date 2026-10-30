// scene.hpp - user facing parameters (shared by the CLI, the ImGui panel and
// the capture tool).
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
  bool head_casts_shadow = false;

  float density = 2.5f;       // shadow density ("shadows" slider)
  float length_var = 0.10f;

  float fuzz_amount = 0.06f, fuzz_distrib = 0.80f, fuzz_exp = 0.0f;
  float frizz_amount = 0.02f, frizz_freq = 1.0f, frizz_exp = 0.0f;
  float kink_amount = 0.02f, kink_freq_uv = 2.0f, kink_freq_w = 1.0f, kink_w_scale = 0.5f, kink_exp = 0.0f;
  float clump_percent = 0.90f, clump_size = 0.0f, clump_noise = 1.0f, clump_freq = 1.0f, clump_var = 0.1f,
        clump_exp = 0.0f;
  float curl_amount = 0.02f, curl_freq = 1.0f, curl_twists = 10.0f, curl_twist_exp = 0.3f, curl_exp = 0.0f;

  // Ribbon width in pixels. Thin ribbons resolve the fibre structure but leave
  // the mass looking like spun sugar; at 3.2 the locks overlap into proper
  // opaque clumps, which is what reads as hair at portrait framing.
  float width_px = 3.2f;
  int hair_count = 34000;
  int segments = 32;

  // ---- silhouette -------------------------------------------------------
  // The guide mesh fixes where strands *can* go, so length and volume are the
  // two controls that actually change the shape: cut_length shortens every
  // strand along its own parameter (a buzz cut is ~0.25, the guide mesh's full
  // length is 1) and volume lifts the mass away from the skull for big hair.
  float cut_length = 1.0f;
  float volume = 0.0f;

  // Fiber (Marschner) parameters - the demo's material LUTs are tuned for these.
  // Defaults are the *rosa* set (see below): the demo's blond tables are still
  // shipped and selectable, but the render starts pink.
  float a_f[3] = {0.68f, 0.36f, 0.42f};
  float r_dev = 8.0f, tt_dev = 10.0f, trt_dev = 15.0f;
  float ambient = 0.30f;

  // Index into the asset bundle's material list (0 = std_blond, 1 = rosa).
  // Selects the Marschner lookup tables *and* the absorption preset.
  // Rosa is the default: the blond tables are kept selectable, not the start.
  int material = 1;

  // ---- hair dye ---------------------------------------------------------
  // The LUTs bake one fibre's absorption, so recolouring them means tinting the
  // scattered radiance. `dye` is the colour at the roots (linear), `dye_tip` the
  // colour at the ends; `dye_gradient` blends between them along the strand -
  // a flat dye is gradient 0, an ombre/balayage is ~0.8, and a "dip dye" is a
  // gradient above 1 (the tips saturate before the mid-lengths).
  // Default: rosa #E8556F, converted to linear the same way the palette does, so
  // the hex shown in the panel matches the swatch that is lit up. (It used to
  // hold a hand-typed darker triple, which made the palette disagree with the
  // start-up look.)
  float dye[3] = {0.8069f, 0.0908f, 0.1595f};
  float dye_tip[3] = {0.8069f, 0.0908f, 0.1595f};  // same by default -> flat dye
  float dye_strength = 0.88f;
  float dye_gradient = 0.0f;
  // 1.0 reproduces mravella/hair's MIN_COLOR ramp exactly (root = 0.5 of the
  // tip value, from MAX_COLOR_START of the strand); 0 turns it off.
  float root_darken = 1.0f;
  float color_variation = 0.22f; // per-strand tint jitter (mravella/hair)
  float sheen = 0.35f;           // extra sky-facing sheen / polish
  float fill_light = 0.22f;      // cool fill from the shadow side (mravella/hair)

  // ---- Unreal's Hair shading model -------------------------------------
  // UE documents the Hair model as "simulating multiple specular highlights:
  // one representing the color of light, and another representing a mix of hair
  // and light color", with the inputs Base Color, Scatter, Specular, Roughness,
  // Tangent and Backlit. Base Colour and Tangent map onto `a_f`/`dye` and the
  // strand tangent; these are the remaining controls, in UE's vocabulary and
  // with UE's artistic ranges (Scatter 0.5-0.8, Specular ~0.5, Backlit 0-0.1,
  // Roughness 0.3-0.5).
  float scatter = 0.65f;   // dual-scattering / GI weight
  float specular = 0.55f;  // scale of the light-coloured highlight lobe
  float backlit = 0.06f;   // transmission through strands lit from behind
  // The demo's extra lift/rim/sheen/saturation nudges have no counterpart in
  // UE's model - they exist for its punchier web look. 1 keeps them all, 0 is
  // the straight physical result.
  float stylise = 0.45f;

  // ---- deep shadow ------------------------------------------------------
  // Unreal shades grooms against a voxelized density volume, and lights that
  // cast "Deep Shadow" use a multi-layer deep opacity map, so absorption
  // saturates at the hair that is actually on the beam instead of growing
  // without bound as the shaded point gets deeper. The density is measured by an
  // extra additive pass in light space (shaders/hair_density.frag, the 2D
  // stand-in for the voxel volume): `deep_shadow` blends that bound into the
  // Weighted blended OIT for the ribbons (see pass_hair_accum / pass_oit): every
  // strand behind the front one keeps contributing, so a mass stays opaque in
  // close-ups instead of collapsing to one layer per pixel. Off restores the
  // coverage-tested path.
  bool oit = true;

  // transmittance (0 = the demo's single-layer Beer-Lambert), `deep_density`
  // scales the measured density, and `hair_ao` uses the same density to occlude
  // ambient light inside the mass.
  float deep_shadow = 0.65f;
  float deep_density = 1.0f;
  float hair_ao = 0.5f;

  // UE's Roughness drives the longitudinal roughness of the three fibre paths.
  // One control, three deviations - the mapping is monotone and hits the
  // defaults at roughness 0.4 (r 8 / tt 10 / trt 15 degrees).
  void set_roughness(float r) {
    r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
    r_dev = 3.0f + 12.5f * r;    // 3 .. 15.5 deg
    tt_dev = 4.0f + 15.0f * r;   // 4 .. 19
    trt_dev = 8.0f + 17.5f * r;  // 8 .. 25.5
  }
  float roughness() const { return std::min(1.0f, std::max(0.0f, (r_dev - 3.0f) / 12.5f)); }
};

// ---- hair dye colour parsing -------------------------------------------
// sRGB hex (#RRGGBB) or a named colour -> linear RGB. The renderer shades in
// linear radiance, so the conversion happens once, here.
//
// `group` is only used by the panel, which lays the swatches out in sections:
// naturals first, then the fashion colours. Names are the CLI vocabulary
// (`--dye copper`), so they are stable even if the panel changes.
struct DyeColor {
  const char* name;
  unsigned rgb;
  const char* group;
};
inline const DyeColor kDyeColors[] = {
    // --- naturals ------------------------------------------------------
    {"jet", 0x14121A, "Natural"},
    {"soft_black", 0x241E28, "Natural"},
    {"raven", 0x2B1720, "Natural"},
    {"espresso", 0x3A2418, "Natural"},
    {"chocolate", 0x4A2C1C, "Natural"},
    {"chestnut", 0x6B3A22, "Natural"},
    {"hazelnut", 0x7E5535, "Natural"},
    {"caramel", 0xA9702F, "Natural"},
    {"honey", 0xC08A3E, "Natural"},
    {"blonde", 0xD9B978, "Natural"},
    {"platinum", 0xEDE4D6, "Natural"},
    {"ash", 0x9A9384, "Natural"},
    {"silver", 0xC6C8CE, "Natural"},
    {"ginger", 0xB4531F, "Natural"},
    {"copper", 0xC0562A, "Natural"},
    {"auburn", 0x7E3018, "Natural"},
    {"mahogany", 0x6A2418, "Natural"},
    {"burgundy", 0x5C1226, "Natural"},
    {"wine", 0x8E1F3A, "Natural"},
    {"cherry", 0xA8182F, "Natural"},
    // --- fashion -------------------------------------------------------
    {"rosa", 0xE8556F, "Fashion"},
    {"pink", 0xF26A85, "Fashion"},
    {"rose", 0xE0596E, "Fashion"},
    {"magenta", 0xC12A8C, "Fashion"},
    {"violet", 0x7B4BCB, "Fashion"},
    {"lilac", 0xC7A9F0, "Fashion"},
    {"lavender", 0xA48CE0, "Fashion"},
    {"teal", 0x2C8C8C, "Fashion"},
    {"emerald", 0x1F9A5A, "Fashion"},
    {"mint", 0x8FE0C0, "Fashion"},
    {"ocean", 0x1E6FBF, "Fashion"},
    {"sky", 0x6FC4E8, "Fashion"},
    {"denim", 0x2C4A72, "Fashion"},
    {"sunset", 0xFF7A3E, "Fashion"},
    {"tangerine", 0xF08A1E, "Fashion"},
    {"gold", 0xE8C36A, "Fashion"},
    {"neon_green", 0x9BEA3A, "Fashion"},
    {"rose_gold", 0xE0A08E, "Fashion"},
    {"smoke", 0x4A4A52, "Fashion"},
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

// The reverse direction, for the panel (which edits sRGB and shows the hex).
inline void linear_to_hair_color(const float in[3], unsigned& rgb) {
  rgb = 0;
  for (int c = 0; c < 3; ++c) {
    float v = in[c] < 0.0f ? 0.0f : (in[c] > 1.0f ? 1.0f : in[c]);
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    rgb = (rgb << 8) | unsigned(std::lround(s * 255.0f));
  }
}

// ---- hairstyle presets -------------------------------------------------
// The demo ships one hair mesh; a "style" here is a full set of operator values
// over that mesh - the technique the SIGGRAPH'24 hair-mesh paper uses to author
// many styles from one guide mesh - plus the two silhouette controls added for
// this port (cut length and volume). `sex` is only a gallery tag.
//
// Every entry is tuned against the renderer: `curl/kink` set the lock structure,
// `clump` how strongly strands gather, `fuzz/frizz` the flyaway halo, `width`
// how much of the mass each ribbon covers, and `cut/volume` the shape.
struct HairstylePreset {
  const char* name;      // CLI id
  const char* label;     // panel label
  const char* sex;       // gallery grouping: Female / Male / Unisex
  float curl_amount, curl_freq, curl_twists;
  float kink_amount, kink_freq_uv;
  float clump_percent, clump_size, clump_var;
  float fuzz_amount, frizz_amount;
  float length_var, width_px;
  int hair_count;
  float cut_length, volume;
  const char* dye_hint;  // suggested dye name (panel colour chip)
};

inline const HairstylePreset kHairstyles[] = {
    // ---- female / long ---------------------------------------------------
    {"bob",     "Bob",            "Female", 0.02f, 1.0f, 10.0f, 0.020f, 2.0f, 0.90f, 0.00f, 0.10f, 0.10f, 0.020f, 0.10f, 2.31f, 20000, 0.62f, 0.10f, "chocolate"},
    {"lob",     "Long bob",       "Female", 0.03f, 1.1f, 11.0f, 0.022f, 2.0f, 0.92f, 0.01f, 0.10f, 0.09f, 0.020f, 0.10f, 2.40f, 22000, 0.74f, 0.14f, "caramel"},
    {"sleek",   "Sleek / glass",  "Female", 0.03f, 1.2f,  9.0f, 0.010f, 1.4f, 0.96f, 0.00f, 0.04f, 0.03f, 0.008f, 0.05f, 2.15f, 26000, 0.96f, 0.04f, "jet"},
    {"long",    "Long & flowing","Female", 0.04f, 1.3f, 11.0f, 0.018f, 1.8f, 0.93f, 0.00f, 0.06f, 0.06f, 0.014f, 0.06f, 2.15f, 20000, 1.00f, 0.08f, "honey"},
    {"layered", "Long layers",    "Female", 0.06f, 1.4f, 12.0f, 0.030f, 2.2f, 0.88f, 0.01f, 0.14f, 0.10f, 0.060f, 0.18f, 2.85f, 24000, 0.92f, 0.16f, "auburn"},
    {"wavy",    "Beach waves",    "Female", 0.05f, 1.5f, 12.0f, 0.030f, 2.4f, 0.86f, 0.01f, 0.20f, 0.08f, 0.030f, 0.14f, 2.80f, 22000, 0.90f, 0.18f, "honey"},
    {"curls",   "Defined curls",  "Female", 0.16f, 2.0f, 16.0f, 0.140f, 3.2f, 0.78f, 0.02f, 0.35f, 0.30f, 0.060f, 0.20f, 3.96f, 24000, 0.80f, 0.26f, "chestnut"},
    {"coils",   "Tight coils",    "Female", 0.22f, 2.4f, 19.0f, 0.200f, 3.6f, 0.66f, 0.02f, 0.42f, 0.38f, 0.090f, 0.24f, 4.20f, 26000, 0.72f, 0.34f, "soft_black"},
    {"afro",    "Afro",           "Unisex", 0.18f, 2.6f, 20.0f, 0.180f, 3.8f, 0.55f, 0.03f, 0.50f, 0.42f, 0.120f, 0.26f, 4.62f, 26000, 0.70f, 0.62f, "soft_black"},
    {"shag",    "Shag",           "Unisex", 0.09f, 1.9f, 15.0f, 0.090f, 3.0f, 0.80f, 0.02f, 0.34f, 0.24f, 0.140f, 0.22f, 3.40f, 24000, 0.70f, 0.24f, "auburn"},
    {"pixie",   "Pixie",          "Female", 0.09f, 1.8f, 14.0f, 0.070f, 2.8f, 0.82f, 0.01f, 0.28f, 0.18f, 0.045f, 0.30f, 3.30f, 22000, 0.42f, 0.12f, "burgundy"},
    {"fringe",  "Blunt fringe",   "Female", 0.02f, 1.0f, 10.0f, 0.014f, 1.8f, 0.95f, 0.00f, 0.05f, 0.04f, 0.010f, 0.05f, 2.25f, 22000, 0.58f, 0.06f, "jet"},
    // ---- male ------------------------------------------------------------
    {"buzz",    "Buzz cut",       "Male",   0.05f, 1.4f,  8.0f, 0.090f, 3.0f, 0.70f, 0.00f, 0.10f, 0.18f, 0.030f, 0.14f, 2.60f, 24000, 0.24f, 0.02f, "soft_black"},
    {"crew",    "Crew cut",       "Male",   0.06f, 1.5f,  9.0f, 0.070f, 2.6f, 0.78f, 0.00f, 0.12f, 0.14f, 0.035f, 0.12f, 2.60f, 24000, 0.34f, 0.05f, "chocolate"},
    {"fade",    "Fade",           "Male",   0.04f, 1.3f,  8.0f, 0.055f, 2.4f, 0.80f, 0.00f, 0.08f, 0.10f, 0.020f, 0.08f, 2.45f, 24000, 0.28f, 0.03f, "jet"},
    {"textured","Textured crop",  "Male",   0.10f, 1.7f, 13.0f, 0.110f, 3.2f, 0.74f, 0.01f, 0.22f, 0.16f, 0.060f, 0.18f, 3.00f, 24000, 0.40f, 0.08f, "ash"},
    {"side_part","Side part",     "Male",   0.03f, 1.2f, 10.0f, 0.020f, 1.8f, 0.94f, 0.00f, 0.05f, 0.05f, 0.015f, 0.06f, 2.30f, 24000, 0.48f, 0.06f, "espresso"},
    {"quiff",   "Quiff",          "Male",   0.05f, 1.5f, 11.0f, 0.040f, 2.2f, 0.88f, 0.01f, 0.12f, 0.08f, 0.040f, 0.10f, 2.70f, 24000, 0.52f, 0.16f, "caramel"},
    {"pompadour","Pompadour",     "Male",   0.04f, 1.3f, 10.0f, 0.030f, 2.0f, 0.92f, 0.01f, 0.10f, 0.06f, 0.030f, 0.08f, 2.55f, 24000, 0.56f, 0.24f, "jet"},
    {"slick_back","Slick back",   "Male",   0.02f, 1.1f,  9.0f, 0.010f, 1.3f, 0.97f, 0.00f, 0.03f, 0.02f, 0.008f, 0.04f, 2.20f, 26000, 0.50f, 0.04f, "soft_black"},
    {"curly_top","Curly top",     "Male",   0.15f, 1.9f, 15.0f, 0.120f, 3.1f, 0.74f, 0.02f, 0.30f, 0.26f, 0.070f, 0.20f, 3.70f, 24000, 0.44f, 0.20f, "chestnut"},
    {"mohawk",  "Faux hawk",      "Unisex", 0.11f, 2.2f, 18.0f, 0.150f, 4.0f, 0.62f, 0.03f, 0.40f, 0.22f, 0.090f, 0.20f, 3.55f, 24000, 0.50f, 0.42f, "teal"},
    // ---- editorial / unisex ---------------------------------------------
    {"punk",    "Spikes",         "Unisex", 0.12f, 3.0f, 24.0f, 0.200f, 4.2f, 0.50f, 0.04f, 0.55f, 0.24f, 0.200f, 0.24f, 3.63f, 24000, 0.46f, 0.30f, "rosa"},
    {"wet_look","Wet look",       "Unisex", 0.03f, 1.2f, 10.0f, 0.012f, 1.5f, 0.99f, 0.00f, 0.02f, 0.01f, 0.005f, 0.03f, 2.05f, 28000, 0.62f, 0.02f, "jet"},
    {"volume",  "Big volume",     "Female", 0.10f, 1.8f, 14.0f, 0.110f, 3.0f, 0.72f, 0.02f, 0.40f, 0.30f, 0.100f, 0.22f, 4.10f, 26000, 0.86f, 0.46f, "honey"},
    {"editorial","Editorial grey","Unisex", 0.07f, 1.6f, 12.0f, 0.060f, 2.6f, 0.86f, 0.01f, 0.18f, 0.14f, 0.050f, 0.14f, 2.90f, 24000, 0.58f, 0.14f, "silver"},
};
inline constexpr int kHairstyleCount = int(sizeof(kHairstyles) / sizeof(kHairstyles[0]));

inline int hairstyle_index(const std::string& name) {
  for (int i = 0; i < kHairstyleCount; ++i)
    if (name == kHairstyles[i].name) return i;
  return -1;
}

// Applies a preset's *style* values. The dye is left alone: the gallery only
// offers the colour as a suggestion, switching style must not throw away a
// colour the user picked.
inline bool apply_hairstyle(struct HairSettings& h, const std::string& name) {
  const int idx = hairstyle_index(name);
  if (idx < 0) return false;
  const HairstylePreset& p = kHairstyles[idx];
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
  h.cut_length = p.cut_length;
  h.volume = p.volume;
  return true;
}

struct SkySettings {
  // Defaults chosen so the key light matches the reference demo's view-space
  // light (upper left, slightly in front) for the default camera.
  float sun_rot = 2.60f;     // sun azimuth (rad, world) - frontal key
  float sun_tilt = -0.55f;   // sun elevation (rad, negative = above)
  float sun_intensity = 1.15f;
  float exposure = 0.72f;
  float bloom = 0.25f;
  float cloud_coverage = 0.15f;
  float vignette = 0.30f;
  bool show_ground = true;
  float ground_y = -0.55f;
  bool show_hair_mesh = false;

  // ---- ground ----------------------------------------------------------
  // A studio checkerboard under the bust. It is generated analytically in
  // ground.frag (no texture fetch): the square size is in world units and the
  // edge is filtered against the screen-space footprint, so it stays crisp up
  // close and fades to the stone instead of aliasing into moire far away.
  float checker = 1.0f;        // 0 = plain stone, 1 = full checkerboard
  float checker_scale = 1.6f;  // squares per world unit (1 = 0.5 m squares)
  float checker_fade = 7.0f;   // distance (m) at which the pattern dissolves
  float checker_a[3] = {0.196f, 0.196f, 0.202f};  // dark square (linear)
  float checker_b[3] = {0.585f, 0.585f, 0.590f};  // light square (linear)

  // ---- atmosphere (Preetham et al. 1999) --------------------------------
  // Turbidity: 2 = crystal clear, 4 = hazy, 10 = very polluted.
  float turbidity = 2.2f;
  float rayleigh = 1.0f;   // Rayleigh multiplier (how blue the sky is)
  float mie = 0.0035f;      // Mie coefficient (aerosol haze)
  float mie_g = 0.80f;     // Mie anisotropy
};

// Weather / liquid effects. All procedural: no extra assets, no textures.
struct WeatherSettings {
  float rain = 0.0f;        // 0 = dry, 1 = downpour
  float wetness = 0.0f;     // surface wetness (0 = track the rain amount)
  float wind = 0.0f;        // wind strength: bends the hair, shears the rain
  float wind_dir = 0.7f;    // wind azimuth (rad)
  float gravity = 0.35f;    // hair sag (0 = weightless, 1 = heavy)
  float puddles = 0.38f;    // how much of the ground collects water
  float lens_drops = 0.0f;  // rain droplets on the virtual lens (0 = track rain)
  float speed = 1.0f;       // effect animation speed
};

struct RenderOptions {
  int width = 768;
  int height = 768;
  int shadow_res = 1024;
  int samples = 4;   // MSAA
  // Render at N x resolution and box-filter down. 2 by default: the hair is
  // made of sub-pixel ribbons, and alpha-to-coverage can only hand out k/4 of a
  // pixel, so a single-sampled frame shows the strand coverage as a stipple.
  // Supersampling averages that away (it is the classic 4x4 rotate/dither
  // pattern in reverse), and this is an offline renderer - quality first.
  int supersample = 2;
  std::string output_png;
  int frames = 1;
  std::string sequence_dir;
  std::string animation = "none";  // none | orbit | sweep | light
  float animation_speed = 1.0f;
  float fps = 30.0f;
  bool bloom = true;
  bool write_json = false;
  bool ui = true;         // draw the ImGui panel into the preview
  std::string ui_dump;    // --ui-dump FILE: panel-only PNG (no Vulkan needed)
  std::string fonts_dir;  // where Roboto + the icon subset live ("" = search)
};

struct Scene {
  CameraSettings camera;
  HairSettings hair;
  SkySettings sky;
  WeatherSettings weather;
};

}  // namespace hgen
