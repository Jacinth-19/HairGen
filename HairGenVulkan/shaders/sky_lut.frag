// sky_lut.frag - physically based sky (single scattering Rayleigh + Mie) baked
// into a small equirectangular table every frame. The table is both the visible
// background and the ambient/IBL source for the head, hair and ground.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const float kPlanetRadius = 6360e3;
const float kAtmosphereRadius = 6420e3;
const vec3 kBetaRayleigh = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float kHr = 8000.0;
const float kHm = 1200.0;
const float kBetaMieScatter = 21e-6;
const float kBetaMieExtinction = 21e-6 / 0.9;

float raySphere(vec3 origin, vec3 dir, float radius) {
  float b = dot(origin, dir);
  float c = dot(origin, origin) - radius * radius;
  float d = b * b - c;
  if (d < 0.0) return -1.0;
  return -b + sqrt(d);
}

vec3 densities(vec3 p) {
  float h = max(0.0, length(p) - kPlanetRadius);
  return vec3(exp(-h / kHr), exp(-h / kHm), 0.0);
}

float rayleighPhase(float mu) { return 3.0 / (16.0 * PI) * (1.0 + mu * mu); }

// Cornette-Shanks approximation of the Mie phase function.
float miePhase(float mu, float g) {
  float g2 = g * g;
  return (3.0 * (1.0 - g2) / (8.0 * PI)) * ((1.0 + mu * mu) / pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

// Optical depth from p towards the sun (8 steps, exponential atmosphere).
vec3 sunOpticalDepth(vec3 p, vec3 sunDir) {
  float tMax = raySphere(p, sunDir, kAtmosphereRadius);
  if (tMax < 0.0) tMax = 0.0;
  float stepLen = tMax / 8.0;
  vec3 od = vec3(0.0);
  for (int i = 0; i < 8; ++i) {
    vec3 q = p + sunDir * (stepLen * (float(i) + 0.5));
    vec3 d = densities(q);
    od += vec3(d.x, d.y, 0.0) * stepLen;
  }
  return od;
}

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
vec3 cloudDeck(vec3 dir, vec3 sunDir) {
  float coverage = frame.skyRef.w;
  if (coverage <= 0.001) return vec3(0.0);
  if (dir.y < 0.012) return vec3(0.0);
  float t = 2600.0 / max(dir.y, 0.012);
  vec2 uv = dir.xz * t * 0.00075;

  // Domain warp so the deck forms filaments instead of round blobs.
  vec2 warp = vec2(fbm(vec3(uv * 0.35, 1.7)), fbm(vec3(uv * 0.35 + 5.3, 8.1))) - 0.5;
  float density = fbm(vec3(uv + warp * 1.4, 4.3));
  density = density * 0.5 + 0.5;

  float threshold = mix(0.72, 0.30, coverage);
  float mask = smoothstep(threshold, threshold + 0.26, density);
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

vec3 atmosphere(vec3 dir, vec3 sunDir) {
  vec3 origin = vec3(0.0, kPlanetRadius + 120.0, 0.0);
  float tMax = raySphere(origin, dir, kAtmosphereRadius);
  if (tMax < 0.0) return vec3(0.0);
  if (raySphere(origin, dir, kPlanetRadius) > 0.0) {
    // looking at the planet itself: return a dim ground bounce
    return vec3(0.018, 0.016, 0.014) * frame.skyRef.x;
  }
  const int steps = 24;
  float stepLen = tMax / float(steps);
  vec3 sumRayleigh = vec3(0.0);
  vec3 sumMie = vec3(0.0);
  vec3 odView = vec3(0.0);
  float mu = dot(dir, sunDir);
  float phaseR = rayleighPhase(mu);
  float phaseM = miePhase(mu, 0.76);
  for (int i = 0; i < steps; ++i) {
    vec3 p = origin + dir * (stepLen * (float(i) + 0.5));
    vec3 d = densities(p);
    odView += vec3(d.x, d.y, 0.0) * stepLen;
    vec3 odSun = sunOpticalDepth(p, sunDir);
    vec3 tau = kBetaRayleigh * (odView.x + odSun.x) +
               vec3(kBetaMieExtinction * (odView.y + odSun.y));
    vec3 attn = exp(-tau);
    sumRayleigh += d.x * attn * stepLen;
    sumMie += d.y * attn * stepLen;
  }
  vec3 color = (sumRayleigh * kBetaRayleigh * phaseR + sumMie * kBetaMieScatter * phaseM);
  // Sun disc (only where the sky is not occluded by the planet).
  float disc = smoothstep(0.99985, 0.99995, mu);
  color += vec3(1.0, 0.92, 0.82) * disc * 14.0;
  return color * frame.skyRef.x;
}

void main() {
  vec2 uv = vUV;
  // Matches equirectUV() in lib/common.glsl:
  //   u = atan(d.z, d.x)/2pi + 0.5, v = acos(d.y)/pi
  float phi = (uv.x - 0.5) * 2.0 * PI;
  float theta = uv.y * PI;
  float ct = cos(theta);
  float st = sqrt(max(0.0, 1.0 - ct * ct));
  vec3 dir = vec3(st * cos(phi), ct, st * sin(phi));
  vec3 sunDir = normalize(frame.lightDir.xyz);
  vec3 color = atmosphere(dir, sunDir);

  color += cloudDeck(dir, sunDir);
  outColor = vec4(max(color, vec3(0.0)), 1.0);
}
