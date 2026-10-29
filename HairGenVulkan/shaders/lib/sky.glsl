// sky.glsl - shared sky helpers: the procedural cloud deck.
//
// Kept out of the baked sky table on purpose: the table is sampled as an
// irradiance probe by skyAmbient(), and a high frequency cloud pattern there
// showed up as blotches all over the head. The visible sky composites the deck
// in background.frag instead.
#ifndef HGEN_SKY_GLSL
#define HGEN_SKY_GLSL

// ---- procedural value noise ------------------------------------------------
// The demo's cloud pass used the tiling 32^3 noise volume, which is too coarse
// to sample at sky scale (trilinear blobs). A hash based value-noise FBM costs
// a few ALU ops per step and gives smooth, scale-free cloud shapes.

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
  vec3 i = floor(p);
  vec3 f = p - i;
  f = f * f * (3.0 - 2.0 * f);
  float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
  float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
  float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
  float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
  float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
  float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
  float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
  float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
  float sum = 0.0, amp = 0.5;
  for (int i = 0; i < 5; ++i) {
    sum += amp * valueNoise(p);
    p = p * 2.07 + vec3(11.3, 7.7, 3.1);
    amp *= 0.5;
  }
  return sum;
}

// Projected cloud deck. Coverage 0 = clear sky, 1 = overcast.
vec3 cloudDeck(vec3 dir, vec3 sunDir, float coverage, float time) {
  if (coverage <= 0.001) return vec3(0.0);
  if (dir.y < 0.012) return vec3(0.0);
  float t = 2600.0 / max(dir.y, 0.012);
  vec2 uv = dir.xz * t * 0.00075 + vec2(time * 0.0025, time * 0.0012);

  // Domain warp so the deck forms filaments instead of round blobs.
  vec2 warp = vec2(fbm(vec3(uv * 0.55, 1.7)), fbm(vec3(uv * 0.55 + 5.3, 8.1))) - 0.5;
  float base = fbm(vec3(uv + warp * 1.1, 4.3));
  // A second, finer octave set breaks the "watercolour" look of pure FBM.
  float detail = fbm(vec3(uv * 3.1 + warp * 0.6, 9.7));
  float density = base * 0.72 + detail * 0.28;
  density = density * 0.5 + 0.5;

  float threshold = mix(0.74, 0.34, coverage);
  float mask = smoothstep(threshold, threshold + 0.16, density);
  // Fade toward the horizon (deck is infinitely far) and thin out overhead.
  mask *= smoothstep(0.0, 0.15, dir.y) * (1.0 - 0.25 * smoothstep(0.8, 1.0, dir.y));
  if (mask <= 0.001) return vec3(0.0);

  // Cheap single-scattering shading: thick parts are shadowed, silver lining
  // where the deck faces the sun.
  float sunFacing = max(dot(dir, sunDir), 0.0);
  float thick = smoothstep(0.30, 0.95, density);
  // Cloud tops scatter white; the undersides pick up the blue of the sky.
  vec3 shade = vec3(0.46, 0.54, 0.68) * (0.75 + 0.25 * (1.0 - thick));
  vec3 lit = vec3(1.0, 0.98, 0.95) * (0.85 + 2.6 * pow(sunFacing, 6.0));
  vec3 color = mix(shade, lit, clamp(density * 1.35, 0.0, 1.0));
  return color * frame.skyRef.x * mask;
}


// Visible sky: the smooth table plus the cloud deck and a sun glow.
vec3 skyWithClouds(vec3 dir) {
  vec3 sunDir = normalize(frame.lightDir.xyz);
  float coverage = frame.skyRef.w;
  float time = frame.camPos.w;
  vec3 color = skyRadiance(dir);
  vec3 clouds = cloudDeck(dir, sunDir, coverage, time);
  // Cloud tops catch the sun, undersides go blue-grey.
  float sunFacing = max(dot(dir, sunDir), 0.0);
  vec2 sunAz = normalize(sunDir.xz + 1e-5);
  vec2 dirAz = normalize(dir.xz + 1e-5);
  float sunSide = pow(max(dot(dirAz, sunAz), 0.0), 2.0);
  clouds *= mix(vec3(0.72, 0.78, 0.95), vec3(1.06, 1.0, 0.95), sunSide);
  color = color * (1.0 - 0.30 * clamp(luma(clouds), 0.0, 1.0)) + clouds;
  // Sun glow, only where the deck is thin.
  color += vec3(1.0, 0.94, 0.84) * pow(sunFacing, 220.0) * 2.4 * frame.skyRef.x;
  return color;
}

#endif
