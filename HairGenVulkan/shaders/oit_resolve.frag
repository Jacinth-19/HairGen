// oit_resolve.frag - order-independent hair resolve.
//
// The accumulation pass draws every strand into one target with additive
// blending, so the pixel ends up holding
//
//     rgb = sum(colour_i * coverage_i)      a = sum(coverage_i)
//
// neither of which depends on the order the strands were rasterised in. Dividing
// the two gives the coverage-weighted mean colour of every strand along the view
// ray, and the summed coverage is how much of the pixel the mass fills: the
// coverage-complement (1 - a) is the revealage of weighted blended OIT, with the
// per-strand product sum(1 - a_i) linearised as 1 - sum(a_i). That is exact for a
// single strand and conservative (never lets the background bleed through a
// covered pixel) for many, which is the behaviour a hair mass wants.
//
// The result is composited *over* the opaque scene, so the head and the ground
// stay exactly as the scene pass left them.
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 5) uniform sampler2D sceneHdr;   // the opaque scene
layout(set = 0, binding = 11) uniform sampler2D oitAccum;  // rgb = colour*w, a = sum(w)

void main() {
  const vec4 base = texture(sceneHdr, vUV);
  const vec4 accum = texture(oitAccum, vUV);
  const float weight = accum.a;
  const float reveal = clamp(1.0 - weight, 0.0, 1.0);
  vec3 hair = base.rgb;
  if (weight > 1e-5) hair = accum.rgb / weight;
  outColor = vec4(mix(hair, base.rgb, reveal), 1.0);
}
