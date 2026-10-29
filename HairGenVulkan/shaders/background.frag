// background.frag - far-field sky for the camera pass: the baked sky table plus
// a screen-space sun glow.
#version 450

#include "lib/common.glsl"
#include "lib/sky.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// frame.invView holds inverse(projection * view), see renderer.cpp.
vec3 viewRay(vec2 uv) {
  vec4 p = frame.invView * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  return normalize(p.xyz / p.w - frame.camPos.xyz);
}

void main() {
  vec3 dir = viewRay(vUV);
  outColor = vec4(skyWithClouds(dir), 1.0);
}
