// head.vert - July's head/upper-body mesh.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vViewPos;
layout(location = 3) out vec3 vLightPos;
layout(location = 4) out vec2 vUV;

void main() {
  vec4 world = vec4(inPos, 1.0);
  gl_Position = frame.mvp * world;
  vWorld = inPos;
  vNormal = inNrm;
  vViewPos = (frame.mv * world).xyz;
  vLightPos = (frame.lightView * world).xyz;
  vUV = inUV;
}
