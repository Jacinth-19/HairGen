// background.frag - far-field sky for the camera pass: the Preetham atmosphere,
// the cloud deck, the sun disc and the rain veil.
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
  vec3 sky = skyWithClouds(dir);

  // Sheets of rain hanging in the air between the camera and the sky. Drawn
  // here (rather than as geometry) because a full screen veil costs nothing and
  // reads correctly at every camera angle.
  vec4 veil = rainVeil(dir);
  sky = sky * (1.0 - veil.a * 0.75) + veil.rgb * veil.a * 0.85;

  outColor = vec4(sky, 1.0);
}
