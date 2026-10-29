// hair_shadow.frag - depth-only hair shadow map, matching the demo's view
// shortening (strands longer than wMax do not cast).
#version 450

layout(location = 4) in float vW;
layout(location = 5) flat in float vWMax;

void main() {
  if (vW > vWMax) discard;
}
