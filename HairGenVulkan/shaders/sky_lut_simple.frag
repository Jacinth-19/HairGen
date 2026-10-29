// sky_lut_simple.frag - debug: flat sky table (no noise / atmosphere work).
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(mix(vec3(0.2, 0.35, 0.7), vec3(0.7, 0.8, 0.95), vUV.y), 1.0); }
