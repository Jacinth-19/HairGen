// head.frag - skin shading X (edit test): wrapped diffuse + GGX specular picking up the hair
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
  vec3 n = normalize(vNormal);
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
      nt.xy *= 0.18;
      vec3 mapped = normalize(mat3(t, b, n) * normalize(nt));
      n = normalize(mix(n, mapped, 0.55));
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
  if (dot(n, n) < 1e-8) n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
  if (dot(n, w_o) < 0.0) n = -n;
  // The bust is a fan of overlapping sheets: whichever sheet wins the depth test
  // brings its own normal, and at sheet boundaries that shows up as patchwork.
  // Leaning the shading normal towards the view direction (a soft, portrait-like
  // wrap) makes neighbouring sheets shade alike, so the seams stop reading.
  n = normalize(mix(n, w_o, 0.75));

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
    n = normalize(mix(n, en, 0.75 * smoothness));
  }


  // The source mesh's UVs run outside [0,1] (u up to 1.2, v down to -1.5); the
  // demo's WebGL sampler wrapped them, so `uv` was wrapped above.
  vec3 albedo = textureGrad(skinAlbedo, uv, duvdx, duvdy).rgb;
  // The photo albedo is quite red and carries its own baked shading; pull it
  // towards neutral and lift the shadows so the face is not a dark mask inside
  // the hair.
  // De-light: the photo carries its own baked shading, so dividing by its
  // low-frequency copy leaves a flat albedo that our lighting can shape.
  vec3 lowFreq = max(textureGrad(skinFlat, uv, duvdx, duvdy).rgb, vec3(0.10));
  vec3 detail = clamp(albedo / lowFreq, vec3(0.0), vec3(1.7));
  // The photo's remaining dark patches are painted hair shadow around the
  // temples and ears; keep only a fraction of the texture detail so they do
  // not read as holes in the skin.
  detail = mix(detail, vec3(luma(detail)), 0.22);
  // The photo's painted hair shadow sits inside the UV island, so dividing it
  // out amplifies it. Keep only a narrow band of texture detail: enough for
  // pores and lip colour, not enough to paint blotches on the forehead.
  // A 0.72..1.30 clamp is almost 2x between its ends, so the photo's remaining
  // shading variation (and its UV-island seams) survived as tonal bands across
  // the face. Keep the range narrow and the amount small: this term is only
  // meant to carry pores, lip colour and the faint brow/nose reddening.
  detail = clamp(detail, vec3(0.90), vec3(1.10));
  detail = mix(vec3(1.0), detail, 0.55);
  // Base skin tone from the demo photo, with the photograph's own lighting
  // divided out, so the shading here is ours.
  const vec3 kSkin = vec3(0.80, 0.62, 0.53);
  albedo = kSkin * detail * 1.20;
  albedo = pow(max(albedo, vec3(0.0)), vec3(2.2));
  albedo = max(albedo, vec3(0.045));

  // Scalp under the hair. The guide mesh leaves a gap above the hairline, and
  // a lit scalp showing through strands is one of the things that read as a
  // bright, flat "flap" on the forehead. Real scalp under hair is in deep
  // occlusion, so darken smoothly above the brow.
  // Only well above the brow (y > 0.55): the forehead has to stay lit because
  // the guide mesh leaves gaps in the strand coverage at the hairline.
  float scalp = smoothstep(0.55, 0.72, vWorld.y);
  albedo *= mix(1.0, 0.30, scalp);

  // Debug taps for the detail maps (dbg 11/12/13 == ao / spec / normal).
  if (frame.misc.z > 10.5 && frame.misc.z < 11.5) { outColor = vec4(vec3(textureGrad(skinAO, uv, duvdx, duvdy).r), 1.0); return; }
  if (frame.misc.z > 11.5 && frame.misc.z < 12.5) { outColor = vec4(vec3(textureGrad(skinSpec, uv, duvdx, duvdy).r), 1.0); return; }
  if (frame.misc.z > 12.5 && frame.misc.z < 13.5) { outColor = vec4(textureGrad(skinNormal, uv, duvdx, duvdy).xyz, 1.0); return; }
  if (frame.misc.z > 13.5 && frame.misc.z < 14.5) { outColor = vec4(uv, 0.0, 1.0); return; }
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

  vec3 sunColor = vec3(1.0, 0.95, 0.89) * frame.skyRef.x * 1.35;

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
  vec3 shadowed = mix(vec3(1.0), trans, 0.22);
  vec3 diffuse = sunColor * wrapLit * mix(shadowed, trans * wrap, 0.45) * albedo;

  // Specular (skin F0 ~ 0.028) with the hair shadow applied.
  vec3 h = normalize(w_o + sunDir);
  float NoH = max(dot(n, h), 0.0);
  float VoH = max(dot(w_o, h), 0.0);
  float a = 0.42;
  float F = 0.028 + 0.972 * pow(1.0 - VoH, 5.0);
  float spec = distributionGGX(NoH, a) * F * ndl * 0.16;
  // The specular mask marks the oily/lip/nail areas; it should modulate the
  // skin highlight rather than be a flat scalar.
  float specMask = frame.misc.z > 16.5 && frame.misc.z < 17.5 ? 1.0
                                 : mix(0.35, 1.0, textureGrad(skinSpec, uv, duvdx, duvdy).r);
  vec3 specular = sunColor * spec * mix(1.0, directF, 0.9) * specMask;

  float ao = frame.misc.z > 15.5 && frame.misc.z < 16.5 ? 1.0 : textureGrad(skinAO, uv, duvdx, duvdy).r;

  ao = mix(1.0, ao * ao, 0.85);  // the map is fairly bright; square to bite

  // Ambient / IBL, plus a view-facing fill so the face keeps form in shadow.
  vec3 ambient = skyAmbient(n, 0.6) * albedo * 1.25 * ao;
  float fillWrap = clamp(dot(n, w_o) * 0.5 + 0.5, 0.0, 1.0);
  vec3 fill = skyAmbient(w_o, 0.9) * albedo * 0.42 * fillWrap * ao;
  // Fake subsurface: skylight through the ears/nose edges.
  float rim = pow(1.0 - max(dot(n, w_o), 0.0), 2.5);
  vec3 sss = sunColor * rim * albedo * 0.03 * (0.3 + 0.7 * clamp(NoL * 0.5 + 0.5, 0.0, 1.0));

  vec3 color = diffuse + specular + ambient + fill + sss + spread * sunColor * 0.02;
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
