# HairGenVulkan - the hair-mesh demo as a native Vulkan / C++ application

A from-scratch native renderer that reproduces the look and the controls of the
`HairGen` WebGL demo (Cem Yuksel's *hair mesh* renderer, `../hairmesh.js` +
`../shaders.js`), written in C++17 on raw Vulkan with **no SDK and no
dependencies beyond a C++ compiler** - the Vulkan entry points are resolved at
runtime and the shaders are pre-compiled to SPIR-V and embedded in the binary.

![contact sheet](media/contact_sheet.jpg)

| | |
|---|---|
| ![rain](media/stills/weather_rain.png) | ![storm](media/stills/weather_storm.png) |

`media/weather_sheet.jpg` walks the whole weather system (dry, drizzle, rain,
storm, sunset, noon, flooded puddles, wet close-up) and `media/rain.gif` /
`media/rain.webp` / `media/rain.avi` are an animated clip of it.

| | |
|---|---|
| ![hero](media/stills/hero.png) | ![back lit](media/stills/light_backlit.png) |

Everything in the frame is GPU work: 10 000 hair strands × 32 segments
(637 836 triangles) rendered as screen-space ribbons, shaded with a Marschner
fiber BSDF that reads the demo's precomputed `N_TT_R / N_TRT / GI / M_GI` lookup
tables, against a physically based sky that is re-baked into an equirectangular
HDR table every frame and then used for both the background and the IBL.

---

## Build

```sh
cd HairGenVulkan
meson setup build --buildtype=release
ninja -C build
./build/hairgen --out render.png --size 768
```

`./setup.sh` does exactly that (and `./setup.sh --capture` also renders the
media below). A plain `Makefile` and a `CMakeLists.txt` are provided as
fallbacks for environments without meson.

Requirements: a C++17 compiler, meson ≥ 0.60, ninja. **No Vulkan SDK** - the
loader is `dlopen`ed and the headers are vendored in `third_party/vulkan`; any
ICD (a real GPU driver, SwiftShader or lavapipe) will do. On a headless box:

```sh
VK_ICD_FILENAMES=/path/to/lvp_icd.json ./build/hairgen --out render.png --size 512
```

The asset bundle (`assets/hairgen_assets.bin`, 4.5 MB) is checked in so the
build is self-contained. It is produced from the web demo's own data by
`node tools/export_assets.js` (`ninja -C build assets`), which re-implements the
demo's hair-mesh 3D-texture / clump-map generation on the CPU.

## Features

**Hair (the point of the demo).** The full hair-mesh pipeline is ported:
topology → 7×6×13 three-channel hair-mesh volume, 22 bundle roots, per-instance
jitter, clump targets from a 256² clump-centre map, kink / curl / frizz / fuzz
displacement in the hair-mesh surface frame, ribbon expansion in screen space
and a length-variation cut-off.

**Pink by default.** The start-up look is the supplied **rosa** LUT set (not
blond): `materials/rosa/*` are re-derived on export so they keep the blond
tables' per-texel luminance -- their scattering structure -- and take only their
chroma from the supplied set. The supplied rosa PNGs are ~5x brighter in N_TRT
than the blond ones; using them raw swamped the R and TT lobes and flattened the
whole head into one pink mass. Blond is still one dropdown away.

**Hair dye.** The fibre lookup tables bake in one hair colour, so a dye is a
*chromatic* remap of the scattered radiance: the target colour is normalised by
its brightest channel (absorption semantics - a dye can only subtract light) and
the fibre's luminance is redistributed into that hue, with the specular bands
kept partly white. Per-strand jitter and a root→tip ramp (the `MIN_COLOR`
gradient from `mravella/hair`) then give the mass depth instead of a flat wig.
Twenty named colours plus any `#RRGGBB`, and the default is **rosa**, not blond.
A Kajiya-Kay wrap-diffuse + tight highlight pair (`sqrt(1-|T·L|)`,
`pow(sqrt(1-|T·H|), 96)`, both from `mravella/hair`) is added on top of the
Marschner tables so the fibres keep a glossy band.

**Hairstyles.** One guide mesh, eight styles: `bob`, `sleek`, `wavy`, `curly`,
`afro`, `pixie`, `long`, `punk`. Each is a full set of fuzz / frizz / kink /
clump / curl operator values - the "author many styles from one hair mesh by
stacking procedural operators" idea from the SIGGRAPH'24 hair-mesh paper.

**Artifact fixes on the scanned bust.** The head is a torn, overlapping
triangle strip, and most of what looked ugly came from that:
normal orientation is now solved per *connected region* (BFS over the welded
edge graph, one global sign), inward normals are repaired against a fitted head
ellipsoid, the normal field is smoothed both over the mesh graph and
*spatially* (a small world-space blur, so co-located sheets of the fold agree
instead of shading as a hard seam), zero-length normals are repaired (they
normalised to NaN and rendered as black wedges), and the head no longer casts a
self-shadow. On the shading side the diffuse uses a wide wrap term, the scalp is
darkened under the hair, and UVs are mirrored back into the island instead of
`fract()`-wrapped (wrapping sampled the far side of the photo, which drew a
tonal seam down the face).

**Skin detail.** The supplied head maps (`normal`, `height`, `ao`, `specular`)
are wired into `head.frag`: a TBN built from UV gradients (the mesh carries no
tangents), a mipmapped tangent-space detail normal, an AO mask that attenuates
ambient + fill, and a specular mask. The raw normal map is one texel of pore
noise over a smooth base, so the exporter derives the shipped normal from the
(clean) height field - high-passed so the big relief is not shaded twice - and
folds in a prefiltered quarter of the original for micro-detail.

**Shading.** Marschner fiber model with the demo's LUTs (`N_TT_R`, `N_TRT`,
`N_TT_R_GI`, `N_TRT_GI`, `GI`, `M_GI`), a multiple-scattering hair shadow model
(Beer-Lambert through a hair depth map, which is what makes blond hair glow when
back-lit) and a Marschner-consistent sky ambient term. The head uses the demo's
skin albedo with wrapped diffuse, GGX specular and hair-shadow transmittance.

**Lighting & shadows.** Two 1 k² shadow maps per frame (object depth + hair
depth for the transmittance model), image-based ambient from the sky table, sun
disc, and an infinite ground plane that catches the ground shadow.

**Sky: Preetham 1999.** The atmosphere is the analytic daylight model from
Preetham, Shirley & Smits -- the same model three.js' `Sky.js` ships, ported to
GLSL here (`shaders/lib/sky_model.glsl`): measured Rayleigh/Mie coefficients at
680/550/450 nm, the `1/(cos θ + 0.15·(93.885 − θ)^−1.253)` optical path, the
solar-zenith intensity curve with its earth-shadow cutoff, Rayleigh and
Henyey-Greenstein phase functions. Because the radiance is physically scaled,
the zenith goes blue, the horizon warms and a low sun reddens on its own; there
is no hand-graded ramp. The table is baked at 256×128 and drives both the
visible background and the IBL probe, with `--turbidity`, `--rayleigh`, `--mie`
and `--mie-g` exposed.

On top of it sits a cloud deck ported from the same reference: sinless gradient
noise with a quintic fade, three drifting octaves, a coverage carve, Beer's law
plus a powder term for the multiple scattering inside the cloud, a
Henyey-Greenstein silver lining around the sun and an aerial-perspective blend
into the horizon. It is composited in `background.frag` only, never baked into
the irradiance table (a high-frequency probe put cloud-shaped blotches on the
face).

**Weather and liquids.** `--rain 0..1` turns on a full rain system:

* **Rain streaks** -- 2 600 instanced billboards generated entirely in the
  vertex shader (no vertex buffer: the instance index seeds a hash for the
  column, fall speed, length and phase). Drops recycle through a 9 m cylinder
  that follows the camera, get sheared by the wind, and are shaded as thin
  glass rods (bright edges, dark core, sun glint, mist at distance).
* **Rain veil** -- low-frequency sheets of water in `background.frag`, so the
  air itself reads wet between the camera and the scene.
* **Wet ground** -- a puddle mask from the noise volume darkens the stone to
  42 %, mirrors the sky with a Schlick Fresnel falloff and adds sharp sun
  glints. Every puddle is dimpled by **expanding rain-impact ripples**: one
  drop per jittered grid cell, with a per-cell period, ring frequency and two
  overlapping wavefronts, whose slope perturbs the reflection normal.
* **Wet skin and wet hair** -- the water film on the face traps the diffuse
  return and tightens the specular lobe; wet strands darken to 58 % and the
  remaining highlight collapses into a hard gloss band, plus a wet rim.
* **Lens droplets** -- two layers of grid-hashed beads in `post.frag` that
  refract the frame beneath them, with a rim and a highlight, drifting down the
  glass.
* **Rain dims the world** -- the sun is cut by up to 55 %, the sky is dragged
  towards grey, and the cloud coverage is pushed towards overcast, so a
  downpour never happens under a clear blue sky.

**Hair physics.** The strands are static geometry, so the dynamics are
evaluated analytically in `hair.vert` as a damped, tapered pendulum pinned at
the root: the free length moves with mass ~ `w^1.7` under a steady wind drag
plus two travelling-wave gust octaves and a sideways flutter, and sags under
`--gravity` (and under the extra weight of the water when it rains). Because the
displacement is a function of `(strand id, w, time)`, the hair still casts a
consistent shadow (the shadow pass runs the same vertex shader).

**Post-processing.** HDR (RGBA16F) scene target → bloom prefilter + separable
4-tap blur at quarter resolution → ACES filmic tonemap, exposure, vignette,
sRGB encode and triangular dithering.

**Anti-aliasing.** 4× MSAA on the scene pass (resolve into a sampled HDR
target), plus dithered tonemapping to kill sky banding. `--samples 1` for A/B.
Strand ribbons use **alpha-to-coverage**: width tapers towards the tip and each
strand carries a small random width/coverage, so the silhouette resolves soft
instead of showing a polygon edge -- with no transparency sorting.

**Camera.** Orbit camera (azimuth / elevation / distance / FOV) with mouse drag
and wheel in the web panel, and `--anim orbit|sweep|light` for turntables.

**Controls.** Exactly the demo's panel - hair colour presets, `shadows`
(density), `length var.`, and the **Fuzz / Frizz / Kink / Clump / Curl** groups
with the same parameter names, ranges and exponent sliders - plus the demo's
render-mode switch, its root-sample and collider toggles and the "show hair
mesh" cage overlay.

**Render modes.** `strips` draws screen-space ribbons; `lines` (the demo's
initial mode, `renderMode = 0`) draws each strand as a lit polyline. Both run
the same vertex-shading path, so the styling parameters affect them identically.

**`show hair mesh`.** The demo's debug view is reproduced from the exported
`hairmesh.pos` volume: one cyan curve per bundle (the five level control points
of the hair-mesh graph) plus the layout-cell outlines.

## Source coverage

Every asset file in the reference demo is consumed by the port. `tools/export_assets.js`
loads them, applies the demo's own centering transform and writes one binary blob.

| demo file | contents | used as |
|---|---|---|
| `july-low.js` | 59 156 lines: `obj_vert_pos`, `obj_normals`, `obj_texCoords`, `obj_indices` | head mesh (`head.*` chunks) |
| `july_hairmesh3.js` | `hairmesh_verts`, `hairmesh_bundles`, `hairmesh_bundleLayout`, `hairmesh_texCoords`, `_bundleTexCoords` | 7×6×13 hair-mesh volume + 22 bundle roots (`hairmesh.*`, `hair.bundles`) |
| `july_rootsamples.js` | 1 000 explicit strip roots `(bundID, s, t, rot)` | root table (`roots.st`, `roots.base`, `roots.count`) - `--root-samples` |
| `july_colliders.js` | 9 head spheres, two spheres each | collider push-out in `hair.vert` - `--no-collide` disables |
| `noise.js` | 32³ RGB noise volume | kink / curl / frizz / fuzz carrier + cloud FBM (`noise3d`) |
| `mtl.js` | `hairMaterial`: `a_f`, `R_dev`, `TT_dev`, `TRT_dev` | fibre absorption + LUT normalisation |
| `shaders.js` | GLSL ES sources | ported to `shaders/*.vert|frag`, `lib/hair_shading.glsl` |
| `three.js` `Sky.js` | Preetham analytic daylight + fBm cloud deck (external reference) | `lib/sky_model.glsl`, `lib/sky.glsl` |
| `textures/july_{normal,height,ao,spec}.png` | head detail maps (1024², supplied separately) | `skin.normal` (height-derived, 512²), `skin.ao`, `skin.spec` |
| `materials/rosa/*` | the rosa Marschner LUT set | `mtl.rosa.*` - `--material rosa` |

External references mined for techniques (nothing is copied verbatim):

| source | taken |
|---|---|
| SIGGRAPH'24 hair-mesh paper | procedural styling operators over one guide mesh (the `--style` presets), texture-space strand layout, LOD thinking |
| `github.com/mravella/hair` | `diffuse = sqrt(1-|T·L|)`, `spec = pow(sqrt(1-|T·H|), shininess)`, per-strand `colorVariation`, root→tip `MIN_COLOR` gradient |

`root.rot` is exported but unused: in the demo it only feeds the (disabled)
simulation path; `obj_vert_nor`/`obj_vert_tex` are unused by the demo itself.

## Usage

```
hairgen --out render.png [options]

  --size N | --width N --height N     resolution (default 768)
  --samples N                          MSAA: 1, 2, 4, 8 (default 4)
  --frames N --sequence DIR --anim NAME --fps N
  --style bob|sleek|wavy|curly|afro|pixie|long|punk   procedural hairstyle
  --dye HEX|NAME                       hair dye (#RRGGBB, or rosa|blonde|copper|
                                       auburn|burgundy|black|violet|teal|silver|
                                       platinum|sunset|mint|lilac|... )
  --dye-strength F                     0 = original fibre colour, 1 = fully dyed
  --root-darken F --color-var F        root shadow / per-strand colour jitter
  --clump F --clump-size F --curl F --curl-twists F --frizz F --kink F --fuzz F
  --density F --length-var F --hair-count N --segments N --width-px F
  --rot R --tilt R --dist D --fov F
  --sun-rot R --sun-tilt R --sun-intensity F --exposure F --bloom F
  --clouds F --vignette F --shadow-res N
  --turbidity F --rayleigh F --mie F --mie-g F    Preetham atmosphere
  --rain F --wetness F --wind F --wind-dir R --gravity F --puddles F
  --lens-drops F                       weather / liquid effects
  --preset dry|drizzle|rain|storm|sunset|noon
  --no-hair --no-head --no-ground
  --lines | --render-mode 0|1          strand lines (demo default) vs ribbons
  --root-samples                       explicit root table from the demo
  --no-collide                         disable head collider push-out
  --show-hairmesh                      overlay the hair-mesh cage
  --serve PORT        interactive panel (see below)
  --list-devices      print the Vulkan adapters
```

### Interactive panel

```sh
./build/hairgen --serve 8099 --size 640
# on a CPU rasteriser use something like
#   --size 384 --samples 1 --hair-count 6000   (~2 frames/s on lavapipe)
```

`http://localhost:8099/` serves the panel in `assets/web/` (fixed preview on the
left, scrollable parameter column on the right, styled after the demo's
`style.css`). The page drives the native renderer over a tiny HTTP API:

| endpoint | purpose |
|---|---|
| `POST /params` | JSON `{ "fuzz": 0.12, "rain": 0.6, "wind": 0.4, "turbidity": 3.0, "renderMode": 0, ... }` scene update |
| `GET /stream` | MJPEG preview of live GPU frames |
| `GET /snapshot.png` | single frame as PNG |
| `GET /scene` | current camera/hair values as JSON |

Connections are served on their own threads and the renderer is guarded by a
render mutex, so the sliders keep working while a stream is live.

## Tests

```sh
meson test -C build          # add VK_ICD_FILENAMES=... to include the render test
```

* `test_math` - vector/matrix math, view-projection round trip.
* `test_png` - the PNG writer emits valid IHDR/IDAT/IEND chunks.
* `test_assets` - the bundle has every chunk the renderer needs, with the demo's
  dimensions (7×6×13 hair mesh, 22 bundles, 256² clump map, ≥25 % coverage).
* `integration_render` - renders a frame headlessly, validates the PNG, checks
  the image is not flat, and verifies the hair pass changes ≥10 % of pixels.
  It skips (meson `77`) when no Vulkan device is available.

## Media (pictures, video, GIF)

`python3 tools/capture.py --binary build/hairgen` (`ninja -C build capture`):

| file | contents |
|---|---|
| `media/stills/*.png` | hero shot, 8 dye swatches, 8 hairstyles, 4 lighting setups, 6 control sweeps, 5 mode stills, 8 weather stills |
| `media/stills/mode_*.png` | lines vs ribbons, root samples, hair-mesh cage, no-collide |
| `media/mode_comparison.png` | the four modes side by side |
| `media/hero_1536.png` | 1536² hero render |
| `media/contact_sheet.jpg` | 12-up overview (hero + dye palette + hairstyles) |
| `media/dye_sheet.jpg` | the full dye palette on one sheet |
| `media/aa_off.png`, `media/aa_on.png` | 1× vs 4× MSAA |
| `media/weather_sheet.jpg` | dry / drizzle / rain / storm / sunset / noon / puddles / close-up |
| `media/rain.gif`, `media/rain.webp`, `media/rain.avi` | the rain system in motion |
| `media/orbit.gif` | 40-frame animated GIF (480², 20 fps) |
| `media/orbit.avi` | MJPEG video of the same animation |
| `media/animation_strip.png` | 6 frames from the turntable |

## Layout

```
src/            main.cpp (CLI), renderer.{hpp,cpp}, vk_util (device/queues/images/
                pipelines/descriptors), vk_loader (runtime loader + ICD scan),
                assets (bundle reader), png (writer), web (preview server), math
shaders/        GLSL 450 sources + lib/common.glsl, lib/shadow.glsl,
                lib/hair_shading.glsl, lib/sky_model.glsl (Preetham),
                lib/sky.glsl (clouds + rain veil)  ->  src/shaders_generated.hpp
                (edit a shader, then `ninja -C build shaders && ninja -C build`;
                 the `shaders-check` target fails the build on a stale header)
tools/          export_assets.js, compile_shaders.py, capture.py
tests/          unit + integration tests (meson)
assets/         hairgen_assets.bin, web/{index.html,style.css,app.js}
media/          rendered deliverables
```

Regenerate shaders with `ninja -C build shaders` (needs `glslangValidator` on
`PATH` or in `tools/bin/`, e.g.
`npm pack glslang-validator-prebuilt-predownloaded`) and the asset bundle with
`ninja -C build assets` (needs node).

## Render graph

```
hair shadow map (1024²) ─┐
object shadow map (1024²)┼─> scene pass (4× MSAA, RGBA16F) ─> resolve
sky LUT bake (256×128) ──┘        │  sky · ground · head · hair
                                  ├─> bloom prefilter (¼ res)
                                  ├─> bloom blur ×2 separable
                                  └─> post: bloom add, exposure, ACES,
                                             vignette, sRGB, dither ─> PNG
```

## Notes / differences from the web demo

* The demo shades straight into sRGB and multiplies by `REAL_INTENSITY = 5`
  followed by a `sqrt()`. Here the scene is linear HDR with the same fiber
  radiance (`REAL_INTENSITY` folded into exposure) and the demo's `sqrt` is
  replaced by a proper ACES tonemap, so the highlights on blond hair roll off
  instead of clipping to white.
* The demo has no background or floor; both are new (baked sky + shadow-catching
  ground plane), as is the bloom/tonemap chain.
* The demo's cloud pass sampled the 32³ noise volume directly, which reads as
  large trilinear blobs at sky scale; the deck here uses gradient-noise FBM
  (ported from three.js' `Sky.js`) with Beer/powder shading and a silver lining.
* `july_colliders.js` is consumed by the demo's physics solver, which is itself
  disabled (`simulator.init()` returns immediately). Here the same spheres are
  applied as a push-out in the hair vertex shader, so they also affect the
  static render.
* `july_rootsamples.js` holds one root per strip (the demo draws at most 1 000).
  Above that, roots are reused cyclically with a small per-strand offset, which
  keeps the demo's root distribution while allowing dense renders.
* The `HGEN_SKIP=passname ...` environment variable skips render passes
  (`background`, `grounddraw`, `headdraw`, `hairdraw`, `rain`, `simplesky`) and
  is handy when bisecting driver problems. `HGEN_DBG=N` replaces the head
  shading with a debug channel (11 ao, 13 normal map, 20 world normal, 22
  albedo, 31 ambient, 32 diffuse, 33 specular, 38 geometric normal, ...).
* `HGEN_NOCULL=1` draws the head two-sided. The default culls back faces, which
  is safe now that the exporter orients every triangle against the supplied
  shading normals; before that fix the scan's folds were drawn inside-out and
  shaded as dark wedges across the face.
* `head.idx` is stored as **u32** in the bundle and the loader must be told so:
  the chunk carried u32 data but was declared u16, so every index was read as
  two halves and the head came out shredded (long thin shards off the
  silhouette, a "flap" across the cheek, smeared shading). `tests/test_assets`
  now pins the format and checks every index is in range.
* One llvmpipe quirk worth knowing: a fragment shader that `discard`s inside an
  alpha-blended, 4× MSAA pass corrupted whole tiles on lavapipe. The rain pass
  therefore writes alpha 0 instead of discarding, which is a no-op under the
  blend function it uses.
