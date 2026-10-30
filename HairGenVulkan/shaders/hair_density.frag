// hair_density.frag - the density integral of the hair along each light ray.
//
// Unreal's groom pipeline voxelizes the strands (`r.HairStrands.Voxelization.*`)
// and shades against that density volume; deep shadows are then resolved from a
// multi-layer deep opacity map. Both exist because a single nearest-strand depth
// map can only measure depth, not *how much hair* the beam crosses: one thin
// strand and forty strands at the same depth look identical to it, and the
// shading has to guess the density from a global constant.
//
// This pass measures it instead. The strands are rasterized once more with no
// depth test into an R16F target with additive blending, so every fragment adds
// its covered area to the texel. The result is the total amount of hair the
// light ray crosses - a line integral of density, the 2D stand-in for UE's
// voxel volume - which the shading uses to bound absorption (absorption cannot
// exceed what is actually there) and to occlude ambient inside the mass.
#version 450

layout(location = 4) in float vW;
layout(location = 5) flat in float vWMax;

layout(location = 0) out vec4 outDensity;

void main() {
  // Same cut rule as the depth pass (hair_shadow.frag): strands longer than
  // wMax do not exist as far as this light is concerned.
  if (vW > vWMax) discard;
  outDensity = vec4(1.0);
}
