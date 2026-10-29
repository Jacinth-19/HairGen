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

// Hair shadow lookup for a light-space point. Returns the tinted transmittance,
// `spread` (light spread into neighbouring fibers) and `directF` (the fraction
// of the direct beam that reaches the point).
vec3 hairShadow(out vec3 spread, out float directF, vec3 pShad) {
  if (any(lessThan(pShad.xy, vec2(0.0))) || any(greaterThan(pShad.xy, vec2(1.0)))) {
    // No strand data here: the beam is unobstructed.
    spread = vec3(0.0);
    directF = 1.0;
    return vec3(1.0);
  }
  const float coverage = 0.05;
  const vec3 a_f = vec3(0.507475266, 0.465571405, 0.394347166);
  const vec3 w_f = vec3(0.028135575, 0.027669785, 0.027669785);
  float hairDensity = frame.fiber2.z;

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
    dir[i] = pow(1.0 - coverage, n);
    t_d[i] = pow(vec3(1.0 - coverage * (1.0 - a_f)), vec3(n));
    spr[i] = n * coverage * w_f;
  }
  directF = bilinear4(dir, f);
  spread = bilinear4(spr, f);
  return bilinear4(t_d, f);
}

// Object visibility * hair transmittance for a world-space light coordinate.
vec3 shadowTransmittance(out vec3 spread, out float directF, vec3 pLight, float bias) {
  vec3 pShad = pLight * 0.5 + 0.5;
  float visObj = objectVisibility(pShad, bias);
  vec3 trans = hairShadow(spread, directF, pShad);
  directF *= visObj;
  return trans * visObj;
}

// Demo-compatible entry point used by the hair shader.
vec3 Shadow(out vec3 spread, out float directF, vec3 pLight, float bias) {
  return shadowTransmittance(spread, directF, pLight, bias);
}

#endif
