// post.frag - HDR resolve: bloom add, exposure, ACES tonemap, vignette, sRGB,
// and a triangular dither to avoid banding in the sky gradient.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PostPC {
  vec4 p;  // x = exposure, y = bloom strength, z = vignette, w = frame index
} pc;

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

// Rain droplets on the virtual lens. Two grid layers of lensing beads: each
// drop refracts the frame underneath it, darkens at its rim and carries a tiny
// highlight, which is what makes a wet lens read as *wet* rather than as a
// dirty overlay.
vec3 lensDroplets(vec2 uv, vec3 sharp, float amount, float time) {
  if (amount <= 0.002) return sharp;
  float aspect = frame.resolution.x / max(frame.resolution.y, 1.0);
  vec3 acc = sharp;
  for (int layer = 0; layer < 2; ++layer) {
    float scale = layer == 0 ? 13.0 : 24.0;
    vec2 st = vec2(uv.x * aspect, uv.y) * scale;
    // Drops crawl down the glass and wander sideways as they go.
    float rowPhase = floor(st.y + time * 0.12 * float(layer + 1));
    st.x += sin(time * 0.4 + rowPhase * 2.3) * 0.18;
    st.y += time * (layer == 0 ? 0.13 : 0.22);

    vec2 id = floor(st);
    vec2 f = fract(st) - 0.5;
    float h = hash12(id + float(layer) * 13.1 + 0.5);
    // More drops as the rain gets heavier; sparse when it is a drizzle.
    if (h > mix(0.24, 0.72, clamp(amount, 0.0, 1.0))) continue;

    vec2 off = (vec2(hash12(id + 3.1), hash12(id + 7.7)) - 0.5) * 0.34;
    float rad = 0.09 + 0.13 * hash12(id + 9.3);
    vec2 p = f - off;
    float d = length(p);
    float mask = smoothstep(rad, rad * 0.62, d);
    if (mask <= 0.002) continue;

    // Sphere-ish normal for the lensing offset.
    float dn = clamp(d / rad, 0.0, 1.0);
    vec2 refr = (p / rad) * 0.035 * (layer == 0 ? 1.0 : 0.55);
    vec3 refracted = texture(hdrTex, clamp(uv + refr, vec2(0.0015), vec2(0.9985))).rgb;

    // Thin bright rim where the drop edge catches the light, dark ring inside.
    float rim = smoothstep(0.70, 1.0, dn) * (1.0 - smoothstep(0.95, 1.0, dn));
    vec2 hlPos = vec2(-rad * 0.35, rad * 0.40);
    float hl = smoothstep(rad * 0.42, 0.0, length(p - hlPos));

    refracted *= 0.90 + 0.35 * rim;
    refracted += vec3(0.80, 0.86, 0.98) * hl * 0.35;
    // Water absorbs a touch of red over its path length.
    refracted *= vec3(0.99, 1.0, 1.01);

    // Droplets sit on the glass out of focus, so they only displace and slightly
    // dim the image underneath instead of stamping an opaque disc on it.
    acc = mix(acc, refracted, mask * clamp(amount * 0.55, 0.0, 0.65));
  }
  return acc;
}

void main() {
  vec3 hdr = texture(hdrTex, vUV).rgb;
  hdr = lensDroplets(vUV, hdr, frame.weather2.z, frame.weather2.w);
  vec3 bloom = texture(bloomTex, vUV).rgb;
  hdr += bloom * pc.p.y;

  hdr *= pc.p.x;

  // Vignette
  vec2 d = vUV - 0.5;
  float vig = 1.0 - pc.p.z * dot(d, d) * 1.6;
  hdr *= clamp(vig, 0.0, 1.0);

  // The fitted ACES curve replaces the Narkowicz fit: it is the one that keeps
  // the chroma of dyed hair in the highlights instead of rolling it to white.
  vec3 mapped = tonemapACESFitted(hdr);
  // Gentle contrast curve keeps the hair detail readable after the filmic knee.
  mapped = mix(mapped, mapped * mapped * (3.0 - 2.0 * mapped), 0.16);

  // Grade: a little more saturation, cool shadows and warm highlights. This is
  // what gives the straw-coloured render its colour separation.
  float l = luma(mapped);
  mapped = mix(vec3(l), mapped, 1.18);
  vec3 shadowTint = vec3(0.94, 0.98, 1.10);
  vec3 highTint = vec3(1.06, 1.01, 0.94);
  mapped *= mix(shadowTint, highTint, smoothstep(0.05, 0.75, l));

  mapped = linearToSRGB(clamp(mapped, 0.0, 1.0));

  // Dither. Two things matter here:
  //
  //  * the *pattern*. A hash built from fract(sin(dot(...))) decorrelates every
  //    pixel from its neighbours, which is exactly what static grain looks like
  //    on a smooth gradient - and on a hair silhouette it reads as the edge
  //    "shimmering". Interleaved gradient noise (Jimenez 2014, the same sequence
  //    Unreal uses for its temporally stable dithering) is a 3x3-diagonal ramp:
  //    it still breaks up banding, but the error is spatially coherent, so it
  //    reads as texture rather than noise.
  //  * the *amplitude*. One 8-bit step, triangular, i.e. +-0.5 LSB: enough to
  //    break an 8-bit quantisation boundary, invisible otherwise.
  vec2 px = floor(vUV * frame.resolution.xy + pc.p.w);
  float ign = fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
  float ign2 = fract(52.9829189 * fract(0.06711056 * (px.x + 1.0) + 0.00583715 * (px.y + 7.0)));
  float dither = (ign + ign2 - 1.0) / 255.0;
  outColor = vec4(mapped + dither, 1.0);
}
