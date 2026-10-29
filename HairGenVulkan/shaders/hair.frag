// hair.frag - Marschner fibre shading with the demo's lookup tables, hair shadow
// transmittance, a dye layer and a sky ambient fill.
//
// Pipeline of the shading model, in order:
//   1. Marschner R / TT / TRT + multiple scattering from the demo's tables
//      (ShadeHair), which carries all of the fibre's angular detail;
//   2. a Kajiya-Kay wrap-diffuse + tight highlight pair (adapted from
//      mravella/hair) so the mass gets a glossy band on top of the tables;
//   3. dye: the radiance is chromatically remapped to the target colour while
//      keeping the table's luminance structure, so dyeing does not flatten the
//      highlights;
//   4. per-strand tint jitter and a root->tip ramp (mravella/hair's MIN_COLOR
//      gradient), which is what turns a mass of fibres into locks;
//   5. sky ambient occlusion towards the roots, a grazing rim, then a soft
//      shoulder tone map.
#version 450

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

void main() {
  if (vW > vWMax) discard;

  vec3 v = normalize(-vViewPos.xyz / vViewPos.w);
  vec3 t = normalize(vTangentView);

  // ---- 1. Marschner ----------------------------------------------------
  vec3 color = ShadeHair(vLightPos, t, v, vInstance);

  // ---- dye -------------------------------------------------------------
  // Unit-luma hue of the target colour: multiplying by it recolours the fibre
  // without touching how bright each scattering lobe is.
  // Absorption semantics: a dye can only *subtract* light, so normalise by the
  // brightest channel (not by luma, which would amplify dark dyes into neon) and
  // then lift the hue back to the fibre's own luminance.
  vec3 dyeCol = frame.dye.rgb;
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
  {
    vec3 Lv = normalize(frame.lightDirView.xyz);
    float directFraction = 0.0;
    vec3 spread2;
    vec3 shadowT = Shadow(spread2, directFraction, vLightPos, 0.0);
    float lit = clamp(luma(shadowT), 0.0, 1.0);
    float TdL = dot(t, Lv);
    float TdH = dot(t, normalize(Lv + v));
    float kkDiff = sqrt(max(0.0, 1.0 - TdL * TdL));
    float kkSpec = pow(sqrt(max(0.0, 1.0 - TdH * TdH)), 96.0);
    vec3 sunC = vec3(1.0, 0.965, 0.93) * frame.skyRef.x * 1.35;
    // The wrap term is tinted by the dye; the highlight stays white.
    color += (mix(vec3(1.0), dyeHue * dyeDark, 0.85) * 0.30 * kkDiff + vec3(1.0) * kkSpec * 0.75) * sunC * lit;
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
  float rootToTip = smoothstep(0.0, 0.9, vW);
  color *= mix(1.0 - frame.dye2.x, 1.0 + 0.30 * frame.dye2.x, rootToTip);

  // ---- 5. sky ambient ------------------------------------------------
  vec3 nApprox = normalize(cross(t, v));
  vec3 tint = mix(frame.fiber.rgb, dyeHue * 0.85 * dyeDark, frame.dye.w);
  float occlusion = mix(0.22, 1.0, smoothstep(0.0, 0.85, vW));
  color += skyAmbient(nApprox, 0.4) * tint * frame.fiber2.w * occlusion;

  // Grazing strands catch the sky: this is what separates the silhouette.
  float rim = pow(1.0 - clamp(abs(dot(nApprox, v)), 0.0, 1.0), 3.0);
  color += (skyAmbient(nApprox, 0.3) + vec3(0.22)) * tint * rim * 0.50 * smoothstep(0.15, 1.0, vW);

  // ---- tone ------------------------------------------------------------
  // Soft shoulder instead of a clip: the fibre radiance has a long tail.
  float l = luma(color);
  color = color / (1.0 + 0.22 * max(l, 0.0));
  // Push saturation back after the dye + ambient mixing, which desaturate.
  color = mix(vec3(luma(color)), color, 1.30);
  // Alpha drives MSAA coverage (alpha-to-coverage), giving soft tips and edges.
  outColor = vec4(max(color, vec3(0.0)), clamp(vAlpha, 0.02, 1.0));
}
