// bloom_blur.frag - separable 9-tap gaussian blur.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform BlurPC {
  vec4 p;  // xy = direction (in uv), z = radius scale
} pc;

void main() {
  const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
  vec2 step = pc.p.xy * pc.p.z;
  vec3 sum = texture(bloomTex, vUV).rgb * w[0];
  for (int i = 1; i < 5; ++i) {
    sum += texture(bloomTex, vUV + step * float(i)).rgb * w[i];
    sum += texture(bloomTex, vUV - step * float(i)).rgb * w[i];
  }
  outColor = vec4(sum, 1.0);
}
