// shadow.glsl - hair shadow-map transmittance + object shadowing.
//
// A port of the multiple-scattering shadow model from the original demo (Cem
// Yuksel's hair mesh renderer): the hair depth map stores the closest strand
// depth; the covered fraction of a light ray then follows Beer-Lambert with a
// fixed per-strand coverage, which yields both the direct visibility and the
// tint of the light that scattered through the hair - what makes blond hair
// glow when it is back-lit.
#ifndef HGEN_SHADOW_GLSL
#define HGEN_SHADOW_GLSL

// 3x3 tent PCF on the object shadow map (hardware bilinear depth comparison).
float objectVisibility(vec3 pLight, float bias) {
  vec2 uv = pLight.xy;
  // Outside the light frustum there is no occluder information; treating it as
  // "in shadow" makes the rim of the hair black, so fall back to fully lit.
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ||
      pLight.z < 0.0 || pLight.z > 1.0)
    return 1.0;
  vec2 texel = 1.0 / vec2(textureSize(objShadMap, 0));
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float weight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
      sum += weight * texture(objShadMap, vec3(uv + vec2(float(x), float(y)) * texel,
                                              pLight.z - bias));
    }
  }
  return sum / 16.0;
}

float bilinear4(float v[4], vec2 f) {
  return mix(mix(v[0], v[1], f.x), mix(v[2], v[3], f.x), f.y);
}

vec3 bilinear4(vec3 v[4], vec2 f) {
  return mix(mix(v[0], v[1], f.x), mix(v[2], v[3], f.x), f.y);
}

// Amount of hair along the light ray at this light-space texel.
//
// hairDensityMap accumulates one unit per covered fragment (see
// hair_density.frag), so this is a line integral of density through the mass -
// the quantity Unreal takes from its voxelized groom. One thin strand and forty
// strands at the same depth are indistinguishable in a nearest-depth map; here
// they are 1 and 40. That is what makes the absorption bound and the mass AO
// below measurable instead of assumed.
float hairMassDensity(vec2 uv) {
  return texture(hairDensityMap, uv).r;
}

// Hair shadow lookup for a light-space point. Returns the tinted transmittance,
// `spread` (light spread into neighbouring fibers) and `directF` (the fraction
// of the direct beam that reaches the point). `massDensity` is the hair density
// along the light ray at this texel, from hairMassDensity().
vec3 hairShadow(out vec3 spread, out float directF, vec3 pShad, float massDensity) {
  if (any(lessThan(pShad.xy, vec2(0.0))) || any(greaterThan(pShad.xy, vec2(1.0)))) {
    // No strand data here: the beam is unobstructed.
    spread = vec3(0.0);
    directF = 1.0;
    return vec3(1.0);
  }
  const float coverage = 0.05;
  // The absorption set follows the active hair material instead of the demo's
  // blond constants, so a rosa/other material does not keep a blond shadow.
  vec3 a_f = clamp(frame.fiber.rgb, vec3(0.02), vec3(0.98));
  const vec3 w_f = vec3(0.028135575, 0.027669785, 0.027669785);
  float hairDensity = frame.fiber2.z;

  // Deep shadow (Unreal's deep opacity map): the absorption along a beam cannot
  // exceed the hair that is actually on it. The single-layer model let n grow
  // with depth without bound, which crushed skin under the fringe to black and
  // turned the interior of the mass into a hole. `deepShadow.x` blends the
  // measured bound in, `deepShadow.z` scales the measured density.
  const float deepAmt = clamp(frame.deepShadow.x, 0.0, 1.0);
  // n is the optical depth of the ray from the nearest strand to this point,
  // computed with the demo's uniform density. The measured mass is the same
  // integral over the *whole* mass, so normalising it against a full head of
  // hair (kFullMassDensity: the measured value for a dense wig at the reference
  // settings) turns it into the fraction of a full mass this ray crosses. A beam
  // through a thin veil then cannot be as dark as one through the whole head,
  // which is the classical single-layer failure the deep opacity map fixes.
  const float kFullMassDensity = 90.0;
  float bound = 1e9;
  if (deepAmt > 0.001)
    bound = max(massDensity / kFullMassDensity * hairDensity * max(frame.deepShadow.z, 0.01), 0.0);

  ivec2 size = textureSize(hairShadMap, 0);
  vec2 t = pShad.xy * vec2(size) + 0.5;
  vec2 f = t - floor(t);
  vec2 s = 0.5 / vec2(size);

  float dir[4];
  vec3 spr[4], t_d[4];
  for (int i = 0; i < 4; ++i) {
    vec2 offs = vec2((i == 1 || i == 3) ? s.x : -s.x, (i >= 2) ? s.y : -s.y);
    float z = texture(hairShadMap, pShad.xy + offs).r;
    float h = max(0.0, pShad.z - z);
    float n = h * hairDensity;
    // Deep shadow: cap the optical path at the measured density of the mass.
    if (deepAmt > 0.001) n = mix(n, min(n, bound), deepAmt);
    dir[i] = pow(1.0 - coverage, n);
    t_d[i] = pow(vec3(1.0 - coverage * (1.0 - a_f)), vec3(n));
    spr[i] = n * coverage * w_f;
  }
  directF = bilinear4(dir, f);
  spread = bilinear4(spr, f);
  return bilinear4(t_d, f);
}

// Object visibility * hair transmittance for a world-space light coordinate.
//
// The hair map is a single depth layer, so a few strands over the forehead
// would otherwise stamp hard blotches on the skin. Nine taps spread over two
// texels turn that into a soft gradient, which is what a real strand cloud does.
vec3 shadowTransmittance(out vec3 spread, out float directF, vec3 pLight, float bias) {
  vec3 pShad = pLight * 0.5 + 0.5;
  float visObj = objectVisibility(pShad, bias);
  const float massDensity = hairMassDensity(pShad.xy);

  vec2 texel = 2.0 / vec2(textureSize(hairShadMap, 0));
  vec3 accT = vec3(0.0), spreadAcc = vec3(0.0);
  float accD = 0.0, weightSum = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float w = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
      vec3 sp;
      float df;
      vec3 offset = vec3(vec2(float(x), float(y)) * texel, 0.0);
      vec3 tr = hairShadow(sp, df, pShad + offset, massDensity);
      accT += tr * w;
      accD += df * w;
      spreadAcc += sp * w;
      weightSum += w;
    }
  }
  accT /= weightSum;
  accD /= weightSum;
  spreadAcc /= weightSum;

  spread = spreadAcc;
  directF = accD * visObj;
  return accT * visObj;
}

// Ambient occlusion of the hair mass, from the same slab thickness: a strand
// buried inside a thick layer of other strands sees far less sky than one on the
// outside. Unreal takes this from the groom's voxelization (shadow, transmittance
// *and* ambient occlusion all come out of the same volume); with two depth layers
// the thickness is the closest available stand-in, and it is what stops the inside
// of the mass from being as bright as its silhouette.
float hairMassAO(vec3 pLight) {
  const float amt = clamp(frame.deepShadow.y, 0.0, 1.0);
  if (amt <= 0.001) return 1.0;
  const vec3 pShad = pLight * 0.5 + 0.5;
  if (any(lessThan(pShad.xy, vec2(0.0))) || any(greaterThan(pShad.xy, vec2(1.0)))) return 1.0;
  // Ambient inside the mass, from the same measured density: a point buried
  // under many strands sees almost no sky, a point on the outer layer sees all
  // of it. Unreal takes exactly this from the groom's voxelization.
  const float d = hairMassDensity(pShad.xy) * max(frame.deepShadow.z, 0.01);
  // 0.012 per unit: a full head of hair (70-130 units) lands between 0.2 and
  // 0.45, which is the range Unreal's groom AO sits in.
  return mix(1.0, exp(-d * 0.012), amt);
}

// Demo-compatible entry point used by the hair shader.
vec3 Shadow(out vec3 spread, out float directF, vec3 pLight, float bias) {
  return shadowTransmittance(spread, directF, pLight, bias);
}

#endif
