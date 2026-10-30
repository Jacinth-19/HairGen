// main.cpp - command line front end: headless stills, frame sequences and the
// live web preview server.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "assets.hpp"
#include "math.hpp"
#include "png.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "vk_util.hpp"
#include "ui.hpp"
#include "web.hpp"

namespace {

void usage() {
  printf(
      "HairGenVulkan - Vulkan / C++ hair-mesh renderer\n"
      "\n"
      "  hairgen --out render.png [options]\n"
      "\n"
      "Output\n"
      "  --out FILE            write a PNG (default render.png)\n"
      "  --size N              square render resolution (default 768)\n"
      "  --width N --height N  explicit resolution\n"
      "  --samples N           MSAA sample count: 1, 2, 4, 8 (default 4)\n"
      "  --ssaa N              supersample N x N and box-filter down (default 1)\n"
      "  --frames N            render N frames (default 1)\n"
      "  --sequence DIR        write DIR/frame_%%04d.png for every frame\n"
      "  --anim NAME           none | orbit | sweep | light (default none)\n"
      "  --fps N               animation frames per second (default 30)\n"
      "\n"
      "Hair\n"
      "  --hair-count N        strands (default 10000)\n"
      "  --segments N          segments per strand (default 32)\n"
      "  --width-px F          ribbon width in pixels (default 1.6)\n"

      "  --clump F --clump-size F --clump-var F --clump-noise F\n"
      "  --curl F --curl-twists F --curl-freq F\n"
      "  --frizz F --frizz-freq F --kink F --kink-freq F --fuzz F\n"
      "  --density F --length-var F --fiber-r/-g/-b F\n"
      "  --no-hair --no-head --no-ground --ground-y F\n"
      "  --material NAME       hair material: std_blond | rosa (own Marschner LUTs)\n"
      "  --style NAME          hairstyle preset (bob|...|mohawk, 26 total, see\n"
      "                        --list-styles); the demo's blond|brown|rosa|curly|\n"
      "                        straight still work as aliases\n"
      "  --dye HEX|NAME        hair dye colour (hex, or rosa|platinum|copper|auburn|\n"
      "                        black|violet|teal|silver|burgundy|sunset)\n"
      "  --dye-strength F      0 = original fibre colour, 1 = fully dyed (default 0.88)\n"
      "  --dye-tip HEX|NAME    ombre tip colour (implies --dye-gradient 0.75)\n"
      "  --dye-gradient F      0 flat, 0.75 ombre, 1.5 dip dye (default 0)\n"
      "  --cut-length F        hair length along the guide mesh (0.15..1, default 1)\n"
      "  --volume F            lift the mass off the skull (0..1, default 0)\n"
      "  --fill F --sheen F    fill light / anisotropic sheen (0..1)\n"
      "  --list-styles         print the hairstyle catalogue\n"
      "  --list-dyes           print the dye palette\n"
      "  --root-darken F       how much darker the roots are (default 0.45)\n"
      "  --color-var F         per-strand tint jitter (default 0.16)\n"
      "  --r-dev D --tt-dev D --trt-dev D  fibre lobe widths (degrees)\n"
      "  --ambient F           hair ambient/skylight strength\n"
      "  --lines               render mode 0: strand centre lines (demo default)\n"
      "  --render-mode N       0 = lines, 1 = ribbons (default 1)\n"
      "  --root-samples        use the demo's explicit root table (july_rootsamples.js)\n"
      "  --no-collide          disable the head collider push-out (july_colliders.js)\n"
      "  --show-hairmesh       overlay the hair-mesh control cage\n"
      "\n"
      "Camera / lighting\n"
      "  --rot R --tilt R --dist D --fov F\n"
      "  --sun-rot R --sun-tilt R --sun-intensity F\n"
      "  --exposure F --bloom F --clouds F --vignette F\n"
      "  --turbidity F --rayleigh F --mie F --mie-g F   Preetham atmosphere\n"
      "\n"
      "Weather / effects\n"
      "  --rain F              rain amount, 0 = dry (default 0)\n"
      "  --wetness F           surface wetness (defaults to the rain amount)\n"
      "  --wind F --wind-dir R wind strength and azimuth for rain and hair\n"
      "  --gravity F           hair sag / weight (default 0.35)\n"
      "  --puddles F           how much of the ground holds water (default 0.55)\n"
      "  --checker F           studio checkerboard amount (0..1, default 1)\n"
      "  --checker-scale F     squares per world unit (default 1.6)\n"
      "  --checker-fade F      distance where the checker dissolves (default 7)\n"
      "  --lens-drops F        rain droplets on the virtual lens (defaults to rain)\n"
      "  --preset NAME         dry | drizzle | rain | storm | sunset | noon\n"
      "  --shadow-res N        shadow map resolution (default 1024)\n"
      "\n"
      "Misc\n"
      "  --assets DIR          asset bundle directory (default ./assets)\n"
      "  --icd PATH            use a specific Vulkan ICD/loader\n"
      "  --list-devices        print Vulkan adapters and exit\n"
      "  --validation          enable the validation layer when available\n"
      "  --serve PORT          run the studio server (dockable ImGui panel streamed to a browser)\n"
      "  --no-ui               serve raw frames only (no panel composited in)\n"
      "  --fonts DIR           directory with Roboto + the icon subset (default assets/fonts)\n"
      "  --help\n");
}

bool match(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

bool file_exists(const std::string& path) {
  if (FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fclose(f);
    return true;
  }
  return false;
}

// Assets live next to the sources, so look in the usual places: an explicit
// --assets path, $HGEN_ASSETS, ./assets, then ../assets relative to the binary
// (that is what a meson build/ tree needs).
std::string resolve_asset_dir(const std::string& requested) {
  std::vector<std::string> candidates;
  if (!requested.empty()) candidates.push_back(requested);
  if (const char* env = std::getenv("HGEN_ASSETS")) candidates.push_back(env);
  candidates.push_back("assets");
  candidates.push_back("../assets");
  candidates.push_back("HairGenVulkan/assets");
  char exe[4096];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n > 0) {
    exe[n] = 0;
    std::string dir(exe);
    size_t slash = dir.find_last_of('/');
    if (slash != std::string::npos) {
      std::string base = dir.substr(0, slash);
      candidates.push_back(base + "/assets");
      candidates.push_back(base + "/../assets");
    }
  }
  for (const std::string& c : candidates)
    if (file_exists(c + "/hairgen_assets.bin")) return c;
  return requested.empty() ? std::string("assets") : requested;
}


// --ui-dump: render the panel over a stand-in background and write a PNG.
//
// It needs no Vulkan at all - the panel is pure CPU work - so it doubles as a
// smoke test for the ImGui integration on any machine, and as the source of the
// panel screenshots in the README.
int run_ui_dump(const std::string& out_path, int width, int height, const std::string& fonts_dir,
                const hgen::Scene& scene) {
  using namespace hgen;
  UiPanel ui;
  std::string dir = fonts_dir;
  if (dir.empty()) {
    const char* candidates[] = {"assets/fonts", "../assets/fonts", "HairGenVulkan/assets/fonts"};
    for (const char* d : candidates) {
      if (FILE* f = fopen((std::string(d) + "/Roboto-Regular.ttf").c_str(), "rb")) {
        fclose(f);
        dir = d;
        break;
      }
    }
  }
  ui.init(dir, width >= 1100 ? 1.35f : (width >= 900 ? 1.15f : 1.0f));
  // HGEN_UI_FOCUS=<id> photographs one panel window at a time (used to make the
  // screenshots in the README): hair, colour, styles, lighting, ground, weather,
  // camera, output, help.
  if (const char* focus = std::getenv("HGEN_UI_FOCUS")) ui.set_solo(focus);
  std::vector<uint8_t> px(size_t(width) * height * 4);
  Scene s = scene;
  for (int frame = 0; frame < 8; ++frame) {
    // Stand-in for the render: a dusk gradient with a soft vignette, so the
    // panel's translucency and the dockspace passthrough are both visible.
    for (int y = 0; y < height; ++y) {
      const float v = float(y) / float(height - 1);
      for (int x = 0; x < width; ++x) {
        const float u = float(x) / float(width - 1);
        const float vignette = 1.0f - 0.35f * ((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f));
        uint8_t* p = &px[(size_t(y) * width + x) * 4];
        p[0] = uint8_t(58.0f * vignette + 24.0f * v);
        p[1] = uint8_t(52.0f * vignette + 18.0f * v);
        p[2] = uint8_t(64.0f * vignette + 30.0f * v);
        p[3] = 255;
      }
    }
    UiStats stats;
    stats.width = width;
    stats.height = height;
    stats.samples = 4;
    stats.fps = 12.3f;
    stats.cpu_ms = 41.0;
    stats.triangles = 637836;
    stats.strands = 34000;
    ui.frame(s, stats, px.data(), width, height, 1.0f / 30.0f);
  }
  write_png_rgba(out_path.c_str(), width, height, px);
  printf("[hairgen] wrote %s (%dx%d panel preview)\n", out_path.c_str(), width, height);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace hgen;
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  RenderOptions opt;
  Scene scene;
  std::string asset_dir = "assets";
  std::string icd;
  bool validation = false;
  bool list_devices = false;
  int serve_port = 0;
  opt.ui = true;
  opt.output_png = "render.png";
  opt.fps = 30.0f;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto next = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for %s\n", what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (match(a, "--help") || match(a, "-h")) { usage(); return 0; }
    else if (match(a, "--out")) opt.output_png = next(a);
    else if (match(a, "--size")) opt.width = opt.height = std::atoi(next(a).c_str());
    else if (match(a, "--width")) opt.width = std::atoi(next(a).c_str());
    else if (match(a, "--height")) opt.height = std::atoi(next(a).c_str());
    else if (match(a, "--samples")) opt.samples = std::atoi(next(a).c_str());
    else if (match(a, "--ssaa")) opt.supersample = std::max(1, std::atoi(next(a).c_str()));
    else if (match(a, "--frames")) opt.frames = std::atoi(next(a).c_str());
    else if (match(a, "--sequence")) opt.sequence_dir = next(a);
    else if (match(a, "--anim")) opt.animation = next(a);
    else if (match(a, "--fps")) opt.fps = float(std::atof(next(a).c_str()));
    else if (match(a, "--shadow-res")) opt.shadow_res = std::atoi(next(a).c_str());
    else if (match(a, "--hair-count")) scene.hair.hair_count = std::atoi(next(a).c_str());
    else if (match(a, "--segments")) scene.hair.segments = std::atoi(next(a).c_str());
    else if (match(a, "--width-px")) scene.hair.width_px = float(std::atof(next(a).c_str()));
    else if (match(a, "--clump")) scene.hair.clump_percent = float(std::atof(next(a).c_str()));
    else if (match(a, "--clump-size")) scene.hair.clump_size = float(std::atof(next(a).c_str()));
    else if (match(a, "--curl")) scene.hair.curl_amount = float(std::atof(next(a).c_str()));
    else if (match(a, "--frizz")) scene.hair.frizz_amount = float(std::atof(next(a).c_str()));
    else if (match(a, "--kink")) scene.hair.kink_amount = float(std::atof(next(a).c_str()));
    else if (match(a, "--fuzz")) scene.hair.fuzz_amount = float(std::atof(next(a).c_str()));
    else if (match(a, "--density")) scene.hair.density = float(std::atof(next(a).c_str()));
    else if (match(a, "--clump-var")) scene.hair.clump_var = float(std::atof(next(a).c_str()));
    else if (match(a, "--clump-noise")) scene.hair.clump_noise = float(std::atof(next(a).c_str()));
    else if (match(a, "--curl-twists")) scene.hair.curl_twists = float(std::atof(next(a).c_str()));
    else if (match(a, "--curl-freq")) scene.hair.curl_freq = float(std::atof(next(a).c_str()));
    else if (match(a, "--curl-exp")) scene.hair.curl_exp = float(std::atof(next(a).c_str()));
    else if (match(a, "--frizz-freq")) scene.hair.frizz_freq = float(std::atof(next(a).c_str()));
    else if (match(a, "--kink-freq")) scene.hair.kink_freq_uv = float(std::atof(next(a).c_str()));
    else if (match(a, "--fiber-r")) scene.hair.a_f[0] = float(std::atof(next(a).c_str()));
    else if (match(a, "--fiber-g")) scene.hair.a_f[1] = float(std::atof(next(a).c_str()));
    else if (match(a, "--fiber-b")) scene.hair.a_f[2] = float(std::atof(next(a).c_str()));
    else if (match(a, "--r-dev")) scene.hair.r_dev = float(std::atof(next(a).c_str()));
    else if (match(a, "--tt-dev")) scene.hair.tt_dev = float(std::atof(next(a).c_str()));
    else if (match(a, "--trt-dev")) scene.hair.trt_dev = float(std::atof(next(a).c_str()));
    else if (match(a, "--ambient")) scene.hair.ambient = float(std::atof(next(a).c_str()));
    else if (match(a, "--length-var")) scene.hair.length_var = float(std::atof(next(a).c_str()));
    else if (match(a, "--material")) {
      std::string s = next(a);
      if (s == "rosa") {
        scene.hair.material = 1;
        scene.hair.a_f[0] = 0.68f; scene.hair.a_f[1] = 0.36f; scene.hair.a_f[2] = 0.42f;
      } else if (s == "blond" || s == "std_blond") {
        scene.hair.material = 0;
        scene.hair.a_f[0] = 0.507475266f; scene.hair.a_f[1] = 0.465571405f;
        scene.hair.a_f[2] = 0.394347166f;
      } else {
        std::fprintf(stderr, "unknown material: %s\n", s.c_str());
        std::exit(2);
      }
    }
    else if (match(a, "--style")) {
      // One flag, two families: the demo panel's original colour/shape buttons
      // (blond | brown | rosa | curly | straight) stay valid as aliases, and
      // everything else is looked up in the hairstyle catalogue. The lookup has
      // to be the *fallback* here - a second `else if (match(a, "--style"))`
      // further down never ran, which made all 26 presets unreachable from the
      // command line.
      const std::string s = next(a);
      if (s == "blond" || s == "std_blond") {
        scene.hair.a_f[0] = 0.507475266f; scene.hair.a_f[1] = 0.465571405f; scene.hair.a_f[2] = 0.394347166f;
      } else if (s == "brown") {
        scene.hair.a_f[0] = 0.18f; scene.hair.a_f[1] = 0.115f; scene.hair.a_f[2] = 0.07f;
      } else if (s == "rosa") {
        scene.hair.a_f[0] = 0.62f; scene.hair.a_f[1] = 0.28f; scene.hair.a_f[2] = 0.33f;
      } else if (s == "curly") {
        scene.hair.curl_amount = 0.075f; scene.hair.curl_twists = 14.0f; scene.hair.curl_freq = 1.6f;
        scene.hair.frizz_amount = 0.05f; scene.hair.curl_exp = 0.35f;
      } else if (s == "straight") {
        scene.hair.curl_amount = 0.004f; scene.hair.frizz_amount = 0.006f; scene.hair.kink_amount = 0.004f;
        scene.hair.fuzz_amount = 0.05f;
      } else if (!hgen::apply_hairstyle(scene.hair, s)) {
        std::fprintf(stderr, "unknown hairstyle: %s (try --list-styles)\n", s.c_str());
        std::exit(2);
      }
    }
    else if (match(a, "--dye")) {
      std::string s = next(a);
      float c[3];
      if (hgen::parse_hair_color(s, c)) { scene.hair.dye[0] = c[0]; scene.hair.dye[1] = c[1]; scene.hair.dye[2] = c[2]; }
      else { std::fprintf(stderr, "unknown dye colour: %s\n", s.c_str()); std::exit(2); }
    }
    else if (match(a, "--dye-strength")) scene.hair.dye_strength = float(std::atof(next(a).c_str()));
    else if (match(a, "--root-darken")) scene.hair.root_darken = float(std::atof(next(a).c_str()));
    else if (match(a, "--color-var")) scene.hair.color_variation = float(std::atof(next(a).c_str()));
    else if (match(a, "--cut-length")) scene.hair.cut_length = float(std::atof(next(a).c_str()));
    else if (match(a, "--volume")) scene.hair.volume = float(std::atof(next(a).c_str()));
    else if (match(a, "--dye-tip")) {
      std::string t = next(a);
      float c[3];
      if (hgen::parse_hair_color(t, c)) { scene.hair.dye_tip[0] = c[0]; scene.hair.dye_tip[1] = c[1]; scene.hair.dye_tip[2] = c[2]; }
      else { std::fprintf(stderr, "unknown tip colour: %s\n", t.c_str()); std::exit(2); }
      if (scene.hair.dye_gradient <= 0.001f) scene.hair.dye_gradient = 0.75f;
    }
    else if (match(a, "--dye-gradient")) scene.hair.dye_gradient = float(std::atof(next(a).c_str()));
    else if (match(a, "--fill")) scene.hair.fill_light = float(std::atof(next(a).c_str()));
    else if (match(a, "--sheen")) scene.hair.sheen = float(std::atof(next(a).c_str()));
    else if (match(a, "--checker")) scene.sky.checker = float(std::atof(next(a).c_str()));
    else if (match(a, "--no-checker")) scene.sky.checker = 0.0f;
    else if (match(a, "--checker-scale")) scene.sky.checker_scale = float(std::atof(next(a).c_str()));
    else if (match(a, "--checker-fade")) scene.sky.checker_fade = float(std::atof(next(a).c_str()));
    else if (match(a, "--list-styles")) {
      for (int i = 0; i < hgen::kHairstyleCount; ++i)
        printf("%-11s %-18s %-7s cut %.2f  volume %.2f  dye %s\n", hgen::kHairstyles[i].name,
               hgen::kHairstyles[i].label, hgen::kHairstyles[i].sex, hgen::kHairstyles[i].cut_length,
               hgen::kHairstyles[i].volume, hgen::kHairstyles[i].dye_hint);
      return 0;
    }
    else if (match(a, "--list-dyes")) {
      for (const auto& c : hgen::kDyeColors) printf("%-12s #%06X  (%s)\n", c.name, c.rgb, c.group);
      return 0;
    }
    else if (match(a, "--render-mode")) scene.hair.render_mode = std::atoi(next(a).c_str());
    else if (match(a, "--lines")) scene.hair.render_mode = 0;
    else if (match(a, "--root-samples")) scene.hair.use_root_samples = true;
    else if (match(a, "--no-root-samples")) scene.hair.use_root_samples = false;
    else if (match(a, "--no-collide")) scene.hair.collide = false;
    else if (match(a, "--show-hairmesh")) scene.hair.show_hairmesh = true;
    else if (match(a, "--no-hair")) scene.hair.show_hair = false;
    else if (match(a, "--no-head")) scene.hair.show_head = false;
    else if (match(a, "--no-ground")) scene.sky.show_ground = false;
    else if (match(a, "--ground-y")) scene.sky.ground_y = float(std::atof(next(a).c_str()));
    else if (match(a, "--rot")) scene.camera.rot = float(std::atof(next(a).c_str()));
    else if (match(a, "--tilt")) scene.camera.tilt = float(std::atof(next(a).c_str()));
    else if (match(a, "--dist")) scene.camera.dist = float(std::atof(next(a).c_str()));
    else if (match(a, "--fov")) scene.camera.fov = float(std::atof(next(a).c_str()));
    else if (match(a, "--sun-rot")) scene.sky.sun_rot = float(std::atof(next(a).c_str()));
    else if (match(a, "--sun-tilt")) scene.sky.sun_tilt = float(std::atof(next(a).c_str()));
    else if (match(a, "--sun-intensity")) scene.sky.sun_intensity = float(std::atof(next(a).c_str()));
    else if (match(a, "--exposure")) scene.sky.exposure = float(std::atof(next(a).c_str()));
    else if (match(a, "--bloom")) scene.sky.bloom = float(std::atof(next(a).c_str()));
    else if (match(a, "--clouds")) scene.sky.cloud_coverage = float(std::atof(next(a).c_str()));
    else if (match(a, "--vignette")) scene.sky.vignette = float(std::atof(next(a).c_str()));
    else if (match(a, "--turbidity")) scene.sky.turbidity = float(std::atof(next(a).c_str()));
    else if (match(a, "--rayleigh")) scene.sky.rayleigh = float(std::atof(next(a).c_str()));
    else if (match(a, "--mie")) scene.sky.mie = float(std::atof(next(a).c_str()));
    else if (match(a, "--mie-g")) scene.sky.mie_g = float(std::atof(next(a).c_str()));
    else if (match(a, "--rain")) scene.weather.rain = float(std::atof(next(a).c_str()));
    else if (match(a, "--wetness")) scene.weather.wetness = float(std::atof(next(a).c_str()));
    else if (match(a, "--wind")) scene.weather.wind = float(std::atof(next(a).c_str()));
    else if (match(a, "--wind-dir")) scene.weather.wind_dir = float(std::atof(next(a).c_str()));
    else if (match(a, "--gravity")) scene.weather.gravity = float(std::atof(next(a).c_str()));
    else if (match(a, "--puddles")) scene.weather.puddles = float(std::atof(next(a).c_str()));
    else if (match(a, "--lens-drops")) scene.weather.lens_drops = float(std::atof(next(a).c_str()));
    else if (match(a, "--preset")) {
      std::string s = next(a);
      if (s == "dry") {
        scene.weather.rain = 0.0f; scene.weather.wetness = 0.0f;
        scene.weather.wind = 0.15f; scene.sky.cloud_coverage = 0.15f;
        scene.weather.lens_drops = 0.0f;
      } else if (s == "drizzle") {
        scene.weather.rain = 0.30f; scene.weather.wetness = 0.55f;
        scene.weather.wind = 0.35f; scene.sky.cloud_coverage = 0.55f;
        scene.weather.lens_drops = 0.25f;
      } else if (s == "rain") {
        scene.weather.rain = 0.65f; scene.weather.wetness = 0.85f;
        scene.weather.wind = 0.55f; scene.sky.cloud_coverage = 0.75f;
        scene.weather.lens_drops = 0.55f;
      } else if (s == "storm") {
        scene.weather.rain = 1.0f; scene.weather.wetness = 1.0f;
        scene.weather.wind = 1.0f; scene.sky.cloud_coverage = 1.0f;
        scene.weather.lens_drops = 0.9f; scene.sky.turbidity = 5.0f;
        scene.sky.exposure = 1.05f; scene.weather.gravity = 0.45f;
      } else if (s == "sunset") {
        // sun_tilt is negative-up. Golden hour: the sun is still ~22 degrees up, so the key reads warm
        // and the sky ramps from gold at the horizon to deep blue overhead.
        // Placing it any lower put the solar disc in frame and the Preetham
        // model's sunset boost blew the whole background to white.
        scene.sky.sun_tilt = -0.38f; scene.sky.turbidity = 3.5f;
        scene.sky.cloud_coverage = 0.40f; scene.weather.wind = 0.3f;
        scene.sky.exposure = 1.3f; scene.sky.bloom = 0.35f;
        scene.sky.sun_rot = 2.1f;
      } else if (s == "noon") {
        scene.sky.sun_tilt = -1.05f; scene.sky.turbidity = 2.0f;
        scene.sky.cloud_coverage = 0.20f; scene.sky.exposure = 0.62f;
      } else {
        std::fprintf(stderr, "unknown preset: %s\n", s.c_str());
        std::exit(2);
      }
    }
    else if (match(a, "--assets")) asset_dir = next(a);
    else if (match(a, "--icd")) icd = next(a);
    else if (match(a, "--list-devices")) list_devices = true;
    else if (match(a, "--validation")) validation = true;
    else if (match(a, "--serve")) serve_port = std::atoi(next(a).c_str());
    else if (match(a, "--no-ui")) opt.ui = false;
    else if (match(a, "--fonts")) opt.fonts_dir = next(a);
    else if (match(a, "--ui-dump")) opt.ui_dump = next(a);
    else {
      fprintf(stderr, "unknown option: %s (try --help)\n", a);
      return 2;
    }
  }

  try {
    if (!opt.ui_dump.empty()) return run_ui_dump(opt.ui_dump, opt.width, opt.height, opt.fonts_dir, scene);
    if (list_devices) {
      Gpu gpu;
      VkApi api;
      load_vulkan(api, icd);
      auto names = hgen::list_devices(api);
      printf("Vulkan provider: %s\n", api.provider.c_str());
      for (auto& n : names) printf("  - %s\n", n.c_str());
      if (names.empty()) printf("  (no devices)\n");
      return 0;
    }

    Gpu gpu;
    gpu.init(icd, validation, false);
    printf("[hairgen] provider: %s\n", gpu.api.provider.c_str());
    printf("[hairgen] device:   %s (%s)\n", gpu.props.deviceName,
           gpu.is_cpu() ? "cpu" : "gpu");

    Assets assets;
    asset_dir = resolve_asset_dir(asset_dir);
    const std::string bundle = asset_dir + "/hairgen_assets.bin";
    assets.load(bundle);

    const int ss = std::max(1, opt.supersample);
    const int render_w = opt.width * ss, render_h = opt.height * ss;
    Renderer renderer(gpu, assets, scene, render_w, render_h, opt.shadow_res, opt.samples);
    printf("[hairgen] %dx%d, %dx MSAA, %d strands x %d segments, %d bundles\n", opt.width, opt.height,
           renderer.samples(), scene.hair.hair_count, scene.hair.segments, 22);

    if (serve_port > 0) {
      WebServer server(gpu, assets, renderer, serve_port);
      server.set_ui_enabled(opt.ui);
      if (!opt.fonts_dir.empty()) server.set_fonts_dir(opt.fonts_dir);
      return server.run();
    }

    // Box-filter a supersampled frame back to the requested resolution.
    auto downsample = [&](const std::vector<uint8_t>& src, int sw, int sh) {
      (void)sh;  // the destination is opt.width x opt.height by construction
      if (ss == 1) return src;
      std::vector<uint8_t> dst(size_t(opt.width) * opt.height * 4);
      const float inv = 1.0f / float(ss * ss);
      for (int y = 0; y < opt.height; ++y)
        for (int x = 0; x < opt.width; ++x) {
          float acc[4] = {0, 0, 0, 0};
          for (int dy = 0; dy < ss; ++dy)
            for (int dx = 0; dx < ss; ++dx) {
              const size_t o = (size_t(y * ss + dy) * sw + (x * ss + dx)) * 4;
              for (int c = 0; c < 4; ++c) acc[c] += float(src[o + c]);
            }
          const size_t d = (size_t(y) * opt.width + x) * 4;
          for (int c = 0; c < 4; ++c) dst[d + c] = uint8_t(clampf(acc[c] * inv, 0.0f, 255.0f));
        }
      return dst;
    };

    // Still frames / sequences.
    const int frames = std::max(1, opt.frames);
    Scene base = scene;
    for (int f = 0; f < frames; ++f) {
      Scene s = base;
      float phase = frames > 1 ? float(f) / float(frames) : 0.0f;
      if (opt.animation == "orbit") s.camera.rot = base.camera.rot + phase * 2.0f * kPi;
      else if (opt.animation == "light")
        s.sky.sun_rot = base.sky.sun_rot + phase * 2.0f * kPi;
      else if (opt.animation == "sweep") {
        float t = 0.5f - 0.5f * std::cos(phase * 2.0f * kPi);
        s.camera.rot = base.camera.rot + t * 1.2f;
        s.sky.sun_rot = base.sky.sun_rot + t * 1.1f;
      }
      renderer.set_scene(s);
      double t = double(f) / double(opt.fps);
      RenderStats stats = renderer.render(t, uint32_t(f));
      if (!opt.sequence_dir.empty()) {
        char name[512];
        snprintf(name, sizeof(name), "%s/frame_%04d.png", opt.sequence_dir.c_str(), f);
        auto px = downsample(renderer.read_pixels(), renderer.width(), renderer.height());
        write_png_rgba(name, opt.width, opt.height, px);
        printf("\r[hairgen] frame %d/%d  %.1f ms  %s     ", f + 1, frames, stats.cpu_ms, name);
      } else {
        auto px = downsample(renderer.read_pixels(), renderer.width(), renderer.height());
        write_png_rgba(opt.output_png, opt.width, opt.height, px);
        printf("[hairgen] wrote %s in %.1f ms (%u triangles)\n", opt.output_png.c_str(), stats.cpu_ms,
               stats.triangles);
      }
      fflush(stdout);
    }
    printf("\n");
    gpu.shutdown();
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
