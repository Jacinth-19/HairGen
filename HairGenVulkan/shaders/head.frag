// head.frag - skin shading: wrapped diffuse + GGX specular picking up the hair
// transmittance shadow, plus sky ambient and a little subsurface tint.
#version 450

#include "lib/common.glsl"
#include "lib/shadow.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vViewPos;
layout(location = 3) in vec3 vLightPos;
layout(location = 4) in vec2 vUV;

layout(location = 0) out vec4 outColor;

float distributionGGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-6);
}

void main() {
  // The source mesh has no tangents, so build the tangent frame from the UV
  // gradients (surface-gradient method) and pull the detail normal in.
  // The mesh's UVs run outside [0,1] (u up to 1.2, v down to -1.5) and the demo's
  // WebGL sampler wrapped them, so wrap explicitly -- but *sample with explicit
  // gradients computed from the unwrapped UVs*. `fract()` is discontinuous, and
  // implicit derivatives taken across a wrap seam select a garbage mip level,
  // which showed up as blocky stair-stepped patches on the cheeks.
  // The island in july.jpg spans roughly [0.05,0.95]^2, but the mesh's UVs run
  // out to u=1.20 and v=-1.50. Wrapping with fract() sends those vertices to the
  // *far* side of the photo (a different face region, hence a different skin
  // tone), which drew a hard tonal seam down the middle of the face. The unwrap
  // is symmetric, so fold the out-of-range values back across the island
  // instead: u>0.95 mirrors to 1.9-u, v<0.05 mirrors to 0.1-v.
  vec2 uv = vUV;
  uv.x = uv.x > 0.95 ? 1.9 - uv.x : uv.x;
  uv.y = uv.y < 0.05 ? 0.10 - uv.y : uv.y;
  uv = clamp(uv, 0.001, 0.999);
  vec2 duvdx = dFdx(vUV), duvdy = dFdy(vUV);
  // Mirroring flips the sign of the U derivative on the reflected half, so the
  // explicit gradients have to follow it.
  if (vUV.x > 0.95) { duvdx.x = -duvdx.x; duvdy.x = -duvdy.x; }
  if (vUV.y < 0.05) { duvdx.y = -duvdx.y; duvdy.y = -duvdy.y; }
  // Feature gates: 15 = no normal map, 16 = no AO, 17 = no spec mask.
  bool noNrm = frame.misc.z > 14.5 && frame.misc.z < 15.5;
  // A zero-length interpolated normal normalises to NaN, and a NaN normal shades
  // as an unlit black wedge with hard edges - one of the defects on this scan.
  // Test the raw length *before* normalising (checking dot(n,n) afterwards can
  // never fire, because NaN compares false) and fall back to the geometric
  // facing normal of the triangle.
  vec3 rawN = vNormal;
  vec3 n = dot(rawN, rawN) > 1e-12 ? normalize(rawN) : normalize(cross(dFdx(vWorld), dFdy(vWorld)));
  if (!noNrm) {
    vec3 dpdx = dFdx(vWorld), dpdy = dFdy(vWorld);
    float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    if (abs(det) > 1e-12) {
      vec3 t = (dpdx * duvdy.y - dpdy * duvdx.y) / det;
      vec3 b = (dpdy * duvdx.x - dpdx * duvdy.x) / det;
      t = normalize(t - n * dot(n, t));
      b = cross(n, t);
      // The supplied normal map carries a lot of pore-level noise; used at
      // full strength it sparkles under the sun. Keep the low frequencies and
      // damp the tangents so the relief reads as skin, not gravel.
      vec3 nt = textureGrad(skinNormal, uv, duvdx, duvdy).xyz * 2.0 - 1.0;
      nt.xy *= 0.16;
      vec3 mapped = normalize(mat3(t, b, n) * normalize(nt));
      n = normalize(mix(n, mapped, 0.30));
    }
  }
  vec3 w_o = normalize(frame.camPos.xyz - vWorld);
  // The source bust is a triangle strip with mixed winding, so roughly half the
  // triangles face away from the camera. The demo shades both sides by flipping
  // the normal on back faces (fs_obj_source: `if (!gl_FrontFacing) n = -n;`);
  // doing the same removes the black patches without touching the mesh.
  // Two-sided shading only: the interpolated normal is flipped to face the
  // viewer. Doing *both* this and a gl_FrontFacing flip double-negates whole
  // triangles wherever the strip's winding disagrees with its vertex normals,
  // which is what produced the staircase-shaped dark patches on the forehead.
  // Safety net: a zero-length interpolated normal normalises to NaN, which
  // renders as an unlit black patch with hard edges. Fall back to the geometric
  // facing normal of the triangle instead.
  if (!(dot(n, n) > 1e-8)) n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
  if (dot(n, w_o) < 0.0) n = -n;
  // The bust is a fan of overlapping sheets: whichever sheet wins the depth test
  // brings its own normal, and at sheet boundaries that shows up as patchwork.
  // Leaning the shading normal towards the view direction (a soft, portrait-like
  // wrap) makes neighbouring sheets shade alike, so the seams stop reading.
  n = normalize(mix(n, w_o, 0.12));

  // The remaining hard wedges are the bust's overlapping sheets: where the scan
  // folds over itself the winning sheet carries a normal pointing into the fold
  // and shades like a crease. Blending towards the normal a smooth head would
  // have at that point (a fitted ellipsoid) makes co-located sheets agree. It
  // only removes normals the scan got wrong; the detail normal map still
  // supplies the fine relief, and the silhouette is unaffected.
  {
    const vec3 kCentre = vec3(0.0, 0.02, -0.06);
    const vec3 kRadii = vec3(0.46, 0.66, 0.52);
    vec3 q = (vWorld - kCentre) / kRadii;
    vec3 en = normalize(q / kRadii);
    if (dot(en, w_o) < 0.0) en = -en;
    float smoothness = smoothstep(-0.85, -0.15, dot(n, en));  // only severely disagreeing normals
    n = normalize(mix(n, en, 0.25 * smoothness));
  }


  // The source mesh's UVs run outside [0,1] (u up to 1.2, v down to -1.5); the
  // demo's WebGL sampler wrapped them, so `uv` was wrapped above.
  vec3 photo = textureGrad(skinAlbedo, uv, duvdx, duvdy).rgb;
  // The photo is display-referred (sRGB), so decode once into linear before it
  // is used as an albedo. Double-decoding made the face milky and flat; not
  // decoding at all made it terracotta.
  vec3 albedo = pow(max(photo, vec3(0.0)), vec3(2.2));
  // The photo carries its own soft frontal lighting. Divide out the low
  // frequency so our sun and sky shape the form, then put back a measured
  // linear skin level so the level stays in range under the scaled sun.
  vec3 lowFreq = pow(max(textureGrad(skinFlat, uv, duvdx, duvdy).rgb, vec3(0.06)), vec3(2.2));
  // The photo is only ~1k across a whole face, so dividing by the blurred copy
  // amplifies pore-scale noise into grain. Keep the ratio in a narrow band and
  // pull it towards unity: enough to keep lips, brows and freckles, not enough
  // to read as dirt (or as dithering) at 700 px.
  vec3 ratio = clamp(albedo / max(lowFreq, vec3(0.02)), vec3(0.55), vec3(1.55));
  ratio = mix(vec3(1.0), ratio, 0.30);
  albedo = ratio * vec3(0.30, 0.185, 0.145);
  // A touch of the photo's own hue survives in the lips, brows and shadow.
  albedo = mix(albedo, pow(max(photo, vec3(0.0)), vec3(2.2)) * 1.6, 0.35);
  // Skin scatters red; nudge the ratio slightly warm without going terracotta.
  albedo = mix(albedo, vec3(luma(albedo)) * vec3(1.05, 1.0, 0.95), 0.12);

  // Scalp under the hair. The guide mesh leaves a gap above the hairline, and
  // a lit scalp showing through strands is one of the things that read as a
  // bright, flat "flap" on the forehead. Real scalp under hair is in deep
  // occlusion, so darken smoothly above the brow.
  // Only well above the brow (y > 0.55): the forehead has to stay lit because
  // the guide mesh leaves gaps in the strand coverage at the hairline.
  float scalp = smoothstep(0.60, 1.15, vWorld.y);
  albedo *= mix(1.0, 0.30, scalp);

  // Debug taps for the detail maps (dbg 11/12/13 == ao / spec / normal).
  if (frame.misc.z > 10.5 && frame.misc.z < 11.5) { outColor = vec4(vec3(textureGrad(skinAO, uv, duvdx, duvdy).r), 1.0); return; }
  if (frame.misc.z > 11.5 && frame.misc.z < 12.5) { outColor = vec4(vec3(textureGrad(skinSpec, uv, duvdx, duvdy).r), 1.0); return; }
  if (frame.misc.z > 12.5 && frame.misc.z < 13.5) { outColor = vec4(textureGrad(skinNormal, uv, duvdx, duvdy).xyz, 1.0); return; }
  if (frame.misc.z > 13.5 && frame.misc.z < 14.5) { outColor = vec4(uv, 0.0, 1.0); return; }
  if (frame.misc.z > 42.5 && frame.misc.z < 43.5) {
    // 43: front faces white, back faces red -- shows which sheets are inverted
    outColor = vec4(gl_FrontFacing ? vec3(0.9) : vec3(0.9, 0.08, 0.08), 1.0);
    return;
  }
  if (frame.misc.z > 41.5 && frame.misc.z < 42.5) {
    // 42: normalized radius from the fitted head ellipsoid (1.0 == on the skin)
    const vec3 kC = vec3(0.0, 0.02, -0.06);
    const vec3 kR = vec3(0.46, 0.66, 0.52);
    vec3 q = (vWorld - kC) / kR;
    outColor = vec4(vec3(length(q) * 0.5), 1.0);
    return;
  }
  if (frame.misc.z > 40.5 && frame.misc.z < 41.5) { outColor = vec4(vUV.x > 1.0 ? 1.0 : 0.0, vUV.y < 0.0 ? 1.0 : 0.0, 0.0, 1.0); return; }

  if (frame.misc.z > 19.5 && frame.misc.z < 20.5) { outColor = vec4(n * 0.5 + 0.5, 1.0); return; }
  if (frame.misc.z > 20.5 && frame.misc.z < 21.5) { outColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0); return; }

  if (frame.misc.z > 21.5 && frame.misc.z < 22.5) { outColor = vec4(albedo, 1.0); return; }  // processed albedo
  if (frame.misc.z > 22.5 && frame.misc.z < 23.5) { outColor = vec4(textureGrad(skinAlbedo, uv, duvdx, duvdy).rgb, 1.0); return; }

  vec3 sunDir = normalize(frame.lightDir.xyz);
  float NoL = dot(n, sunDir);
  float ndl = max(NoL, 0.0);

  // Normal-offset the receiver in world space before projecting: at the grazing
  // sun angles this portrait uses, a pure depth bias leaves large stair-stepped
  // self-shadow patches on the face.
  float slope = sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.15);
  vec3 receivePos = vWorld + n * (0.010 + 0.030 * slope);
  vec3 pLight = (frame.lightView * vec4(receivePos, 1.0)).xyz;
  float bias = 0.02 + 0.20 * slope;
  float directF;
  vec3 spread;
  vec3 trans = shadowTransmittance(spread, directF, pLight, bias);


  // The bust is lit by the same sun as the hair, but skin albedo (~0.43) is an
  // order of magnitude above the fibre albedo, so the skin needs its own gain:
  // without it the face clips to white under the physically scaled sun.
  vec3 sunColor = vec3(1.0, 0.95, 0.89) * frame.skyRef.x * 0.72;

  // Wrapped diffuse with a wide wrap term. The bust is a fan of overlapping
  // sheets, so its real creases turn the surface away from the sun; a hard
  // max(NoL,0) turns every one of those creases into a black wedge. Skin is a
  // strongly scattering medium, so light wraps far past the terminator -- which
  // is both more physical here and hides the scan's folds.
  const float kWrap = 0.62;
  float wrap = clamp((NoL + kWrap) / (1.0 + kWrap), 0.0, 1.0);
  float wrapLit = wrap * wrap;   // soft terminator, not a linear ramp
  // Hair shadows land on the face as broad soft bands; keep them as a tint
  // rather than a hard mask so the features stay readable.
  if (frame.misc.z > 29.5 && frame.misc.z < 30.5) { trans = vec3(1.0); directF = 1.0; }  // no shadow
  // A dense curtain of strands blocks nearly all of the direct beam, which
  // turned the side of the face under the hair into a black patch. Real hair
  // leaks: keep a floor on both the direct fraction and the transmitted tint so
  // the skin stays readable and only *dims*.
  trans = max(trans, vec3(0.42));
  directF = max(directF, 0.25);
  vec3 shadowed = mix(vec3(1.0), trans, 0.18);
  vec3 diffuse = sunColor * wrapLit * mix(shadowed, trans * wrap, 0.35) * albedo;

  // Specular (skin F0 ~ 0.028) with the hair shadow applied.
  vec3 h = normalize(w_o + sunDir);
  float NoH = max(dot(n, h), 0.0);
  float VoH = max(dot(w_o, h), 0.0);
  float a = 0.58;
  float F = 0.028 + 0.972 * pow(1.0 - VoH, 5.0);
  float spec = distributionGGX(NoH, a) * F * ndl * 0.10;
  // The specular mask marks the oily/lip/nail areas; it should modulate the
  // skin highlight rather than be a flat scalar.
  float specMask = frame.misc.z > 16.5 && frame.misc.z < 17.5 ? 1.0
                                 : mix(0.35, 1.0, textureGrad(skinSpec, uv, duvdx, duvdy).r);
  vec3 specular = sunColor * spec * mix(1.0, directF, 0.9) * specMask;

  float ao = frame.misc.z > 15.5 && frame.misc.z < 16.5 ? 1.0 : textureGrad(skinAO, uv, duvdx, duvdy).r;

  ao = mix(1.0, ao * ao, 0.85);  // the map is fairly bright; square to bite

  // Ambient / IBL, plus a view-facing fill so the face keeps form in shadow.
  // The skylight probe is now a physically scaled Preetham atmosphere, so these
  // weights are ratios against the sun rather than arbitrary gains. Skin in
  // shade is roughly 1/8 of sunlit skin, which is what keeps the face from
  // turning into a flat white mask.
  vec3 ambient = skyAmbient(n, 0.6) * albedo * 0.34 * ao;
  float fillWrap = clamp(dot(n, w_o) * 0.5 + 0.5, 0.0, 1.0);
  vec3 fill = skyAmbient(w_o, 0.9) * albedo * 0.10 * fillWrap * ao;

  // Portrait fill light.
  //
  // With a single key light the far side of a face is lit only by the sky, and
  // a physically correct sky is not enough: the cheek, the far eye socket and
  // the jaw collapse into a murky grey-blue mass with no form left in it. Every
  // portrait has a fill for exactly this reason. It is a soft, slightly warm
  // source sitting just off the camera axis, shaped by the surface normal (so
  // it still has form) and by the AO map (so it stays out of the creases).
  vec3 fillDir = normalize(w_o + vec3(0.0, 0.25, 0.0) +
                           normalize(cross(w_o, vec3(0.0, 1.0, 0.0)) + 1e-5) * 0.45);
  float nFill = clamp(dot(n, fillDir) * 0.5 + 0.5, 0.0, 1.0);
  vec3 fillLight = vec3(1.0, 0.94, 0.90) * sunColor;
  vec3 beauty = fillLight * albedo * 0.13 * nFill * nFill * ao;
  // Fake subsurface: skylight through the ears/nose edges.
  float rim = pow(1.0 - max(dot(n, w_o), 0.0), 2.5);
  vec3 sss = sunColor * rim * albedo * 0.03 * (0.3 + 0.7 * clamp(NoL * 0.5 + 0.5, 0.0, 1.0));

  vec3 color = diffuse + specular + ambient + fill + beauty + sss + spread * sunColor * 0.02;

  // ---- rain: a water film on the skin ---------------------------------
  // A wet surface loses a lot of its diffuse return (the water layer traps the
  // light) and gains a much tighter, brighter specular lobe, which is exactly
  // what separates "someone standing in the rain" from "a dry statue".
  float wet = clamp(frame.weather.y, 0.0, 1.0);
  if (wet > 0.001) {
    color = mix(color, color * vec3(0.74, 0.76, 0.82), wet * 0.55);
    float specWet = distributionGGX(NoH, 0.12) * (0.08 + 0.92 * F) * ndl;
    color += sunColor * specWet * specMask * wet * 0.55;
    // Sky and hair light reflected off the film.
    vec3 rWet = reflect(-w_o, n);
    color += skyAmbient(rWet, 0.12) * wet * 0.10 * F * ao;
  }

  if (frame.misc.z > 30.5 && frame.misc.z < 31.5) color = ambient;
  if (frame.misc.z > 31.5 && frame.misc.z < 32.5) color = diffuse;
  if (frame.misc.z > 32.5 && frame.misc.z < 33.5) color = specular;
  if (frame.misc.z > 33.5 && frame.misc.z < 34.5) color = fill;
  if (frame.misc.z > 34.5 && frame.misc.z < 35.5) color = vec3(directF);
  if (frame.misc.z > 35.5 && frame.misc.z < 36.5) color = trans;
  if (frame.misc.z > 36.5 && frame.misc.z < 37.5) color = vec3(ndl);
  if (frame.misc.z > 38.5 && frame.misc.z < 39.5) {
    vec3 fn = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    color = fn * 0.5 + 0.5;
  }
  if (frame.misc.z > 37.5 && frame.misc.z < 38.5) color = vec3(luma(trans), luma(spread) * 4.0, 0.0);
  outColor = vec4(color, 1.0);
}
