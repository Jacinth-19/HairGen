// ground.frag - infinite procedural ground plane that catches the head and hair
// shadows (the demo has no floor at all). Ray-traced against y = groundY inside a
// fullscreen pass so the horizon is exact and needs no tessellation.
//
// When it rains the floor turns into a wet surface: a puddle mask darkens the
// stone, mirrors the sky with a Fresnel falloff, and every puddle is dimpled by
// expanding rain-impact ripples.
#version 450

#include "lib/common.glsl"
#include "lib/shadow.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 viewRay(vec2 uv) {
  vec4 p = frame.invView * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  return normalize(p.xyz / p.w - frame.camPos.xyz);
}

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

// Slightly wavy matte studio floor.
float groundAlbedo(vec2 xz) {
  float n = texture(tex_noise, vec3(xz * 0.08, 0.5)).r;
  float n2 = texture(tex_noise, vec3(xz * 0.31, 0.2)).g;
  return 0.085 + 0.030 * n + 0.015 * n2;
}

// Where water collects. Two octaves of the tiling noise volume give large
// pooling areas with a ragged, believable edge.
float puddleMask(vec2 xz, float puddles) {
  if (puddles <= 0.001) return 0.0;
  float n = texture(tex_noise, vec3(xz * 0.085, 0.5)).r * 0.72 +
            texture(tex_noise, vec3(xz * 0.26, 0.13)).g * 0.28;
  float lo = mix(0.90, 0.58, puddles);
  return smoothstep(lo, lo + 0.10, n);
}

// Rain impact ripples: one expanding ring per drop, seeded per grid cell.
// Returns the surface slope (dx, dz) so it can tilt the reflection normal.
vec2 rainRipples(vec2 xz, float t) {
  const float cellSize = 0.42;
  vec2 cell = floor(xz / cellSize);
  vec2 slope = vec2(0.0);
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      vec2 c = cell + vec2(float(x), float(y));
      float h0 = hash12(c);
      float h1 = hash12(c + 17.3);
      float h2 = hash12(c + 41.7);
      // Each cell gets a drop that re-impacts on its own cycle.
      float period = 0.70 + h0 * 1.30;
      float cyc = fract(t / period + h1);
      vec2 centre = (c + vec2(h1, h2)) * cellSize;
      vec2 d = xz - centre;
      float r = length(d);
      float radius = cyc * 0.34;
      float ring = r - radius;
      // Gaussian ring, dying out as the wave expands and the impact fades.
      float w = 0.045;
      float amp = exp(-ring * ring / (w * w)) * (1.0 - cyc) * (1.0 - cyc);
      amp *= smoothstep(0.0, 0.05, cyc);
      // Ring frequency varies per cell, otherwise the grid of impacts reads as a
      // regular lattice of ripples.
      float freq = 55.0 + h2 * 70.0;
      if (r > 1e-4) slope += (d / r) * amp * sin(ring * freq) * 0.7;
      // A second, slower ring so the surface never looks metronomic.
      float cyc2 = fract(t / (period * 1.7) + h2);
      float r2 = r - cyc2 * 0.24;
      float amp2 = exp(-r2 * r2 / (w * w * 1.6)) * (1.0 - cyc2);
      if (r > 1e-4) slope += (d / r) * amp2 * sin(r2 * (38.0 + h0 * 30.0)) * 0.35;
    }
  }
  return slope;
}

void main() {
  if (frame.misc.y < 0.5) discard;
  vec3 dir = viewRay(vUV);
  float groundY = frame.misc.x;
  if (dir.y > -1e-4) discard;  // looking up: no plane hit
  float t = (groundY - frame.camPos.y) / dir.y;
  if (t < 0.0) discard;
  vec3 world = frame.camPos.xyz + dir * t;

  // Analytical depth for the hit point so the head/hair occlude the floor.
  vec4 clip = frame.mvp * vec4(world, 1.0);
  gl_FragDepth = clamp(clip.z / clip.w, 0.0, 1.0);

  float wet = clamp(frame.weather.y, 0.0, 1.0);
  float time = frame.weather2.w;

  // ---- surface ---------------------------------------------------------
  float albedo = groundAlbedo(world.xz);
  float puddle = puddleMask(world.xz, frame.weather2.y) * wet;

  // Wet stone is much darker than dry stone: the water film stops the diffuse
  // return at the surface and lets the light scatter inside the layer instead.
  albedo *= mix(1.0, 0.42, wet * 0.85);
  albedo *= mix(1.0, 0.70, puddle);

  // Surface normal: the ripples only exist where there is standing water.
  vec3 n = vec3(0.0, 1.0, 0.0);
  if (wet > 0.001) {
    float rippleAmt = frame.weather.x * puddle;
    if (rippleAmt > 0.001) {
      vec2 slope = rainRipples(world.xz, time);
      n = normalize(vec3(-slope.x * 0.075 * rippleAmt, 1.0, -slope.y * 0.075 * rippleAmt));
    }
    // A little micro-relief everywhere: the wet film is never perfectly flat.
    float g1 = texture(tex_noise, vec3(world.xz * 1.3, 0.7)).r - 0.5;
    n = normalize(n + vec3(g1, 0.0, g1 * 0.7) * 0.10 * wet);
  }

  vec3 pLight = (frame.lightView * vec4(world, 1.0)).xyz;
  float bias = 0.0045;  // the floor is nearly edge-on to the sun: bias generously
  float directF;
  vec3 spread;
  vec3 trans = shadowTransmittance(spread, directF, pLight, bias);
  // Only inside the shadow map footprint do the hair/object shadows apply.
  vec2 shadUV = pLight.xy * 0.5 + 0.5;
  float inside = smoothstep(0.0, 0.06, min(min(shadUV.x, 1.0 - shadUV.x), min(shadUV.y, 1.0 - shadUV.y)));
  float ndl = max(dot(n, normalize(frame.lightDir.xyz)), 0.0);
  vec3 sunColor = vec3(1.0, 0.95, 0.88) * frame.skyRef.x * 1.7;
  // The sun is dimmed by rain (both by the clouds and by the water in the air).
  sunColor *= 1.0 - 0.55 * clamp(frame.weather.x, 0.0, 1.0);

  vec3 direct = sunColor * ndl * mix(vec3(1.0), trans, inside);
  vec3 ambient = skyAmbient(n, 1.0) * 0.16;

  // A little hair-colored light bleeding onto the floor under the shadow.
  vec3 bounce = spread * sunColor * inside * 0.6;

  vec3 color = (direct + ambient) * albedo + bounce * 0.5;

  // ---- water: sky reflection + specular --------------------------------
  if (wet > 0.001) {
    vec3 v = -dir;
    float NoV = max(dot(n, v), 0.0);
    float F = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);   // Schlick, water-ish F0
    vec3 r = reflect(dir, n);
    r.y = abs(r.y);                                // only the sky above reflects
    // Puddles are smooth mirrors; wet stone is a rough, broken reflection.
    vec3 refl = skyAmbient(r, mix(0.65, 0.06, puddle));
    float gloss = mix(0.18, 0.95, puddle);
    color = mix(color, refl, clamp(F * gloss * 0.95, 0.0, 0.62));
    // Sharp sun glints off the ripples.
    vec3 h = normalize(v + normalize(frame.lightDir.xyz));
    float spec = pow(max(dot(n, h), 0.0), mix(18.0, 420.0, puddle));
    color += sunColor * spec * mix(0.10, 0.85, puddle) * 0.35;
    // Wet film sheen in the ambient.
    color += skyAmbient(n, 0.35) * wet * 0.05;
  }

  // Contact occlusion under the bust so it sits on the floor instead of
  // floating above it.
  float r2 = length(world.xz - vec2(0.0, 0.0));
  color *= mix(0.35, 1.0, smoothstep(0.18, 1.05, r2));

  // Distance haze towards the horizon colour.
  // Aerial perspective. The sky probe is physically scaled now, so the fog is a
  // fraction of the horizon radiance rather than a fixed grey: it fades the
  // floor into the sky without bleaching it.
  // The fade has to land *on* the sky, not on a fraction of it: the old
  // `horizon * 0.34` left the far plane 3x darker than the sky above it, so the
  // floor ended in a hard bright seam right at eye level (very visible through
  // the semi-transparent hair). Converge to the sky radiance a few degrees above
  // the horizon instead, which is what a real floor meets.
  float haze = 1.0 - exp(-t * 0.020);
  vec3 horizon = skyRadiance(normalize(vec3(dir.x, 0.055, dir.z))) * 0.88;
  color = mix(color, horizon, haze * 0.97);

  outColor = vec4(color, 1.0);
}
