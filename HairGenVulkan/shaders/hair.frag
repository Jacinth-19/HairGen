// hair.frag - Marschner fibre shading with the demo's lookup tables, hair shadow
// transmittance, a two-tone dye layer, a fill light and a sky ambient fill.
//
// Pipeline of the shading model, in order:
//   1. Marschner R / TT / TRT + multiple scattering from the demo's tables
//      (ShadeHair), which carries all of the fibre's angular detail;
//   2. a Kajiya-Kay wrap-diffuse + tight highlight pair (adapted from
//      mravella/hair) so the mass gets a glossy band on top of the tables;
//   3. dye: the radiance is chromatically remapped to the target colour while
//      keeping the table's luminance structure, so dyeing does not flatten the
//      highlights. The colour itself can run from root to tip (ombre, balayage,
//      dip dye) - see frame.dye3.w;
//   4. per-strand tint jitter and a root->tip ramp (mravella/hair's MIN_COLOR
//      gradient), which is what turns a mass of fibres into locks;
//   5. a cool fill light from the shadow side (mravella/hair's FILL_LIGHT_POS),
//      so the dark half of the head still has shape;
//   6. sky ambient occlusion towards the roots, a grazing rim, sheen, then a
//      soft shoulder tone map.
#version 450

// ---------------------------------------------------------------- constants
// Lifted verbatim from mravella/hair's constants.glsl so this file can be
// diffed against it. Do not retune these to taste: they are that project's
// look, and the numbers below are the reason the highlight reads as hair
// rather than as plastic.
const float HAIR_SHININESS  = 50.0;  // exponent of the Kajiya-Kay highlight
const float MIN_COLOR       = 0.50;  // value at the root, relative to the tip
const float MAX_COLOR_START = 0.30;  // strand parameter where the ramp begins

#include "lib/common.glsl"
#include "lib/shadow.glsl"
#include "lib/hair_shading.glsl"

layout(location = 0) in vec4 vViewPos;
layout(location = 1) in vec3 vTangentView;
layout(location = 2) in vec3 vLightPos;
layout(location = 3) in vec3 vWorld;
layout(location = 4) in float vW;
layout(location = 5) flat in float vWMax;
layout(location = 6) flat in int vInstance;
layout(location = 7) in float vAlpha;

layout(location = 0) out vec4 outColor;

// Root -> tip colour. `g < 1` is an ombre that ends on the tip colour; `g > 1`
// is a dip dye whose band covers the last 1/g of the strand; `g == 0` is flat.
vec3 dyeAlongStrand(float tipParam) {
  float g = frame.dye3.w;
  vec3 rootCol = frame.dye.rgb;
  if (g <= 0.001) return rootCol;
  float tipMix;
  if (g <= 1.0) {
    tipMix = smoothstep(1.0 - g, 1.0, tipParam);
  } else {
    float band = 1.0 / g;
    tipMix = smoothstep(1.0 - band, 1.0 - band * 0.45, tipParam);
  }
  return mix(rootCol, frame.dye3.rgb, clamp(tipMix, 0.0, 1.0));
}

void main() {
  if (vW > vWMax) discard;

  vec3 v = normalize(-vViewPos.xyz / vViewPos.w);
  vec3 t = normalize(vTangentView);

  bool isDyed = frame.dye.w > 0.001;

  // ---- 1. Marschner ----------------------------------------------------
  float lit;  // fraction of the direct beam that reaches this point
  vec3 color = ShadeHair(vLightPos, t, v, vInstance, lit);
  // Dense strand masses block the direct beam almost completely (that is the
  // point of the multiple-scattering shadow model), but a real head of hair is
  // never black on top: light keeps bouncing between the fibres. Lift the
  // deeply shadowed part towards the fibre's own tint.
  color += frame.fiber.rgb * 0.16;

  // ---- dye -------------------------------------------------------------
  // Unit-luma hue of the target colour: multiplying by it recolours the fibre
  // without touching how bright each scattering lobe is.
  // Absorption semantics: a dye can only *subtract* light, so normalise by the
  // brightest channel (not by luma, which would amplify dark dyes into neon) and
  // then lift the hue back to the fibre's own luminance.
  float tipParam = clamp(vW / max(vWMax, 1e-3), 0.0, 1.0);
  vec3 dyeCol = dyeAlongStrand(tipParam);
  float dyeMax = max(max(dyeCol.r, dyeCol.g), max(dyeCol.b, 1e-3));
  vec3 dyeChroma = dyeCol / dyeMax;
  float dyeHueLuma = max(luma(dyeChroma), 0.45);
  vec3 dyeHue = dyeChroma / dyeHueLuma;   // roughly unit-luma hue
  // Dark dyes must also *darken*: without this every colour would come out at
  // the fibre's own brightness, which turned auburn and burgundy into neon.
  float dyeDark = clamp(pow(max(dyeMax, 0.02), 0.80), 0.18, 1.10);

  // ---- 2. Kajiya-Kay sheen (mravella/hair) -----------------------------
  // diffuse = sqrt(1-|T.L|), specular = pow(sqrt(1-|T.H|), shininess). Cheap,
  // and it supplies the tight bright band the lookup tables smear out.
  vec3 Lv = normalize(frame.lightDirView.xyz);
  lit = clamp(lit, 0.0, 1.0);
  {
    float TdL = dot(t, Lv);
    float TdH = dot(t, normalize(Lv + v));
    float kkDiff = sqrt(max(0.0, 1.0 - TdL * TdL));
    float kkSpec = pow(sqrt(max(0.0, 1.0 - TdH * TdH)), HAIR_SHININESS);
    vec3 sunC = vec3(1.0, 0.965, 0.93) * frame.skyRef.x * 1.35;
    // The wrap term is tinted by the dye; the highlight stays white.
    color += (mix(vec3(1.0), dyeHue * dyeDark, 0.85) * 0.28 * kkDiff + vec3(1.0) * kkSpec * 0.50) * sunC * lit;
  }

  // ---- 3. dye ----------------------------------------------------------
  // Keep the fibre's luminance, redistribute it into the dye's hue.
  vec3 dyed = vec3(luma(color)) * dyeHue * dyeDark;
  // Dark dyes are also less saturated: at these radiances a unit-luma hue turns
  // auburn into neon orange, so pull the darkest dyes toward neutral.
  dyed = mix(dyed, vec3(luma(dyed)), 0.40 * (1.0 - dyeDark));
  // Highlights keep a little of the fibre's own whiteness: fully dyed hair still
  // has white specular bands, and losing them makes it look like matte plastic.
  float hl = smoothstep(1.1, 3.4, luma(color));
  dyed = mix(dyed, vec3(luma(color)) * mix(vec3(1.0), dyeChroma, 0.25), hl * 0.55);
  color = mix(color, dyed, frame.dye.w);

  // ---- 4. strand identity ---------------------------------------------
  float rv = Rand(vInstance * 7 + 11, 5) * 2.0 - 1.0;
  color *= 1.0 + frame.dye2.y * rv;
  // mravella/hair's MIN_COLOR ramp, using its own constants: the root of a
  // strand is MIN_COLOR (half) the value of the tip, and the ramp has fully
  // arrived by MAX_COLOR_START of the strand. The slider is a *faithfulness*
  // blend - 1 reproduces the source exactly, 0 turns the ramp off - rather than
  // an invented curve that merely looks similar. (The dye's own root->tip ramp
  // is separate: that one is hue, this one is value.)
  float rootToTip = mix(MIN_COLOR, 1.0, smoothstep(MAX_COLOR_START, 1.0, vW));
  color *= mix(1.0, rootToTip, clamp(frame.dye2.x, 0.0, 1.0));

  // ---- 5. sky ambient --------------------------------------------------
  vec3 nApprox = normalize(cross(t, v));
  vec3 tint = isDyed ? mix(frame.fiber.rgb, dyeHue * 0.85 * dyeDark, frame.dye.w) : frame.fiber.rgb;
  float occlusion = mix(0.22, 1.0, smoothstep(0.0, 0.85, vW));
  vec3 sky = skyAmbient(nApprox, 0.4);
  color += sky * tint * frame.fiber2.w * occlusion;

  // ---- fill light (mravella/hair) --------------------------------------
  // A cool, shadowless lamp opposite the key. Hair is a volume of thin
  // scatterers, so without it the shadow side collapses into a silhouette; it
  // is deliberately weak and blue-shifted so it reads as bounce, not a second
  // sun.
  float fillAmt = clamp(frame.hair6.z, 0.0, 1.5);
  if (fillAmt > 0.001) {
    vec3 fillDir = normalize(vec3(-Lv.x, 0.38, -Lv.z) + vec3(1e-4));
    float fillDiff = sqrt(max(0.0, 1.0 - dot(t, fillDir) * dot(t, fillDir)));
    vec3 fillCol = mix(sky, vec3(dot(sky, vec3(0.3333))), 0.35) * 0.85 + vec3(0.015);
    color += fillCol * tint * fillDiff * fillAmt * occlusion * 1.2;
  }

  // ---- sheen / polish ---------------------------------------------------
  // Keep the fibre's own anisotropic sheen: a broad lobe that brightens where
  // the fibre turns away from the camera. At 0 the hair is matte; at 1 it looks
  // freshly washed. Uses the demo's unused "sheen" slider slot.
  float sheenAmt = clamp(frame.dye2.z, 0.0, 1.5);
  if (sheenAmt > 0.001) {
    float sheenTerm = pow(1.0 - clamp(abs(dot(t, v)), 0.0, 1.0), 2.2);
    color += (sky * 1.15 + vec3(0.05)) * tint * sheenAmt * sheenTerm * 0.42 *
             smoothstep(0.10, 0.85, vW);
  }

  // Grazing strands catch the sky: this is what separates the silhouette.
  float rim = pow(1.0 - clamp(abs(dot(nApprox, v)), 0.0, 1.0), 3.0);
  color += (sky * 1.10 + vec3(0.16)) * tint * rim * 0.55 * smoothstep(0.15, 1.0, vW);

  // ---- 6. rain ---------------------------------------------------------
  // Wet hair darkens strongly (the water fills the gaps between cuticle scales
  // and kills the internal scattering) and the remaining highlight becomes a
  // tight, bright band: that combination is the classic "just came in from the
  // rain" look.
  float wet = clamp(frame.weather.y, 0.0, 1.0);
  if (wet > 0.001) {
    color *= mix(1.0, 0.58, wet);
    vec3 hWet = normalize(Lv + v);
    float tWet = pow(sqrt(max(0.0, 1.0 - dot(t, hWet) * dot(t, hWet))), 220.0);
    color += vec3(1.0, 0.99, 0.97) * tWet * wet * 1.25 * lit;
    // Beaded droplets: the strand silhouette picks up a wet rim.
    color += sky * tint * wet * rim * 0.55;
  }

  // ---- tone ------------------------------------------------------------
  // Soft shoulder instead of a clip: the fibre radiance has a long tail.
  float l = luma(color);
  color = color / (1.0 + 0.22 * max(l, 0.0));
  // Push saturation back after the dye + ambient mixing, which desaturate.
  color = mix(vec3(luma(color)), color, 1.30);
  // Alpha drives MSAA coverage (alpha-to-coverage), giving soft tips and edges.
  outColor = vec4(max(color, vec3(0.0)), clamp(vAlpha, 0.02, 1.0));
}
