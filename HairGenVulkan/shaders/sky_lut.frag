// sky_lut.frag - physically based sky (single scattering Rayleigh + Mie) baked
// into a small equirectangular table every frame. The table is both the visible
// background and the ambient/IBL source for the head, hair and ground.
#version 450

#include "lib/common.glsl"
#include "lib/sky.glsl"

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

  // ---- look: a deeper, more saturated sky with a warm horizon ---------------
  // Physically the table is already correct; the extra saturation and the warm
  // horizon scoop are deliberate grading so the blond/rosa hair separates from
  // the background instead of sitting on a grey wall.
  color = mix(vec3(luma(color)), color, 1.5);
  float horizon = exp(-max(dir.y, 0.0) * 9.0);
  vec2 sunAz = normalize(sunDir.xz + 1e-5);
  vec2 dirAz = normalize(dir.xz + 1e-5);
  float sunSide = pow(max(dot(dirAz, sunAz), 0.0), 2.0);
  color += vec3(1.0, 0.52, 0.20) * horizon * (0.05 + 0.55 * sunSide) * frame.skyRef.x * 0.55;
  color *= vec3(1.0, 1.0, 1.07);   // a touch more blue in the body of the sky

  // NB: the cloud deck is *not* baked into this table. It is high frequency and
  // the table doubles as the ambient/IBL source, which turned the deck into
  // blotches on the face. background.frag adds the clouds for the visible sky.
  outColor = vec4(max(color, vec3(0.0)), 1.0);
}
