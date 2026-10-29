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

void main() {
  vec3 hdr = texture(hdrTex, vUV).rgb;
  vec3 bloom = texture(bloomTex, vUV).rgb;
  hdr += bloom * pc.p.y;

  hdr *= pc.p.x;

  // Vignette
  vec2 d = vUV - 0.5;
  float vig = 1.0 - pc.p.z * dot(d, d) * 1.6;
  hdr *= clamp(vig, 0.0, 1.0);

  vec3 mapped = tonemapACES(hdr);
  // Gentle contrast curve keeps the hair detail readable after the filmic knee.
  mapped = mix(mapped, mapped * mapped * (3.0 - 2.0 * mapped), 0.15);
  mapped = linearToSRGB(mapped);

  float dither = (hash(vUV * frame.resolution.xy + pc.p.w) - 0.5) / 255.0;
  outColor = vec4(mapped + dither, 1.0);
}
