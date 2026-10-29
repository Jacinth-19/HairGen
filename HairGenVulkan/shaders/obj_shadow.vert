// obj_shadow.vert - head silhouette into the object shadow map.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec3 inPos;

void main() {
  gl_Position = frame.lightMVP * vec4(inPos, 1.0);
}
