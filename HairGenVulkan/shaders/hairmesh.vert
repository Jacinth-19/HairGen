// hairmesh.vert - the "show hair mesh" overlay: positions are already in world
// space, so this just projects them.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec3 inPos;

void main() { gl_Position = frame.mvp * vec4(inPos, 1.0); }
