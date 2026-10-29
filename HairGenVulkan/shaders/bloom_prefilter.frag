// bloom_prefilter.frag - bright pass + 4x downsample for the bloom chain.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
  vec2 texel = frame.resolution.zw * 2.0;
  vec3 sum = vec3(0.0);
  sum += texture(hdrTex, vUV + vec2(-texel.x, -texel.y)).rgb;
  sum += texture(hdrTex, vUV + vec2(texel.x, -texel.y)).rgb;
  sum += texture(hdrTex, vUV + vec2(-texel.x, texel.y)).rgb;
  sum += texture(hdrTex, vUV + vec2(texel.x, texel.y)).rgb;
  sum *= 0.25;
  float l = luma(sum);
  // Soft-knee threshold so only genuinely bright areas bloom.
  float knee = 0.7;
  float contrib = max(l - knee, 0.0) / max(l, 1e-4);
  outColor = vec4(sum * contrib, 1.0);
}
