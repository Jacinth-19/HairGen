// sky.glsl - the visible sky: Preetham atmosphere + volumetric-looking cloud
// deck, plus the weather hooks (rain haze, overcast, lens droplets).
//
// The cloud model is a port of the reference cloud layer from three.js'
// Sky.js, which is the best-looking freely available implementation of a
// cheap sky deck:
//   * smooth gradient noise (quintic fade) instead of plain value noise,
//   * 4-octave fBm with a per-octave drift so the deck shears with the wind,
//   * coverage carve via smoothstep(1 - cov, 1.3 - cov, n),
//   * "beer" (transmittance) + "powder" (multiple scattering) shading,
//   * a Henyey-Greenstein silver lining around the sun,
//   * aerial perspective so distant cloud fades into the horizon haze.
//
// Kept out of the baked sky table on purpose: the table is sampled as an
// irradiance probe by skyAmbient(), and a high frequency cloud pattern there
// showed up as blotches all over the head. The visible sky composites the deck
// here instead.
#ifndef HGEN_SKY_GLSL
#define HGEN_SKY_GLSL

#include "lib/sky_model.glsl"

// ---- gradient noise --------------------------------------------------------
// sinless hash, from the "Hash without Sine" collection (Dave Hoskins) - the
// same family of hash three.js' cloud shader uses. Better distribution than the
// fract(p*0.1031) trick, which shows directional banding on large fBm domains.
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec2 gradientAt(vec2 cell) {
  float a = hash12(cell) * 6.28318530718;
  return vec2(cos(a), sin(a));
}

// Perlin-style gradient noise, quintic fade: no visible cell alignment.
float gradNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = p - i;
  vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  float a = dot(gradientAt(i), f);
  float b = dot(gradientAt(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
  float c = dot(gradientAt(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
  float d = dot(gradientAt(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 1.7 + 0.5;
}

// 4 octaves with drift; the drift makes the deck shear instead of sliding.
float cloudFbm(vec2 uv, vec2 drift) {
  float sum = 0.0;
  float amp = 0.5;
  float freq = 1.0;
  for (int i = 0; i < 4; ++i) {
    sum += amp * gradNoise(uv * freq + drift * (1.0 + float(i) * 0.35));
    amp *= 0.5;
    freq *= 2.03;
  }
  return clamp(sum * 1.16, 0.0, 1.0);
}

// ---- cloud deck ------------------------------------------------------------
// dir: view direction. Returns premultiplied cloud radiance (rgb) and coverage
// in alpha, so the caller can composite it over the sky properly.
vec4 cloudDeck(vec3 dir, vec3 sunDir, float coverage, float time, float wind) {
  float ele = dir.y;
  if (coverage <= 0.002 || ele < 0.004) return vec4(0.0);
  sunDir = normalize(sunDir);

  // Project onto the deck plane. Dividing by (elevation + k) instead of the raw
  // elevation keeps the projection finite at the zenith: the raw ray/plane
  // intersection stretches the noise to infinity overhead, which tore the deck
  // into hard-edged blobs directly above the camera.
  vec2 cloudUV = dir.xz / (ele + 0.32) * 1.15;

  vec2 windDir = vec2(cos(frame.weather.w), sin(frame.weather.w));
  vec2 drift = windDir * (time * (0.010 + 0.045 * wind));
  cloudUV += drift;

  // Three octaves, each drifting a little faster than the one below it, so the
  // deck shears instead of sliding as a rigid sheet.
  float n0 = cloudFbm(cloudUV * 0.45, drift);
  float n1 = cloudFbm(cloudUV * 1.15 + 17.3, drift * 1.3);
  float n2 = cloudFbm(cloudUV * 2.70 + 41.7, drift * 1.8);
  float density = clamp(n0 * 0.55 + n1 * 0.30 + n2 * 0.15, 0.0, 1.0);

  // Coverage carve. `coverage` 0 leaves a few wisps, 1 is overcast.
  float cov = clamp(coverage, 0.0, 1.0);
  float lo = mix(1.02, 0.18, cov);
  float mask = smoothstep(lo, lo + 0.26, density);
  // The deck is effectively infinitely far away: fade it into the horizon haze.
  mask *= smoothstep(0.0, 0.09, ele);
  if (mask <= 0.003) return vec4(0.0);

  float depth = clamp((density - lo) / 0.45, 0.0, 1.0);

  // Beer's law through the deck and the powder term that stands in for the
  // multiple scattering inside it.
  float beer = exp(-3.2 * depth);
  float powder = 1.0 - beer * beer;
  float cosT = max(dot(dir, sunDir), 0.0);
  float shade = clamp(beer * powder * 2.4, 0.0, 1.0);
  shade = mix(0.44, 1.0, shade);

  // Silver lining: Henyey-Greenstein, g = 0.7, around the sun.
  const float g = 0.7;
  float silver = 0.42 * pow(max(1.49 - 1.4 * cosT, 1e-3), -1.5);

  float sunUp = smoothstep(-0.08, 0.32, sunDir.y);
  vec3 sunTint = mix(vec3(1.00, 0.58, 0.32), vec3(1.0, 0.97, 0.92), sunUp);
  vec3 skyBounce = mix(vec3(0.26, 0.31, 0.44), vec3(0.48, 0.55, 0.72),
                       smoothstep(-0.15, 0.5, sunDir.y));

  vec3 color = skyBounce * shade + sunTint * (shade * shade) * (0.85 + silver) * 0.55;
  color *= 0.80 + 0.30 * mask;

  float wet = clamp(frame.weather.x, 0.0, 1.0);
  color = mix(color, vec3(0.26, 0.27, 0.30) * (0.55 + 0.45 * shade), wet * 0.8);

  // Aerial perspective: distance clouds wash out into the horizon.
  float aerial = pow(1.0 - clamp(ele, 0.0, 1.0), 4.0);
  vec3 horizon = preethamSky(normalize(vec3(dir.x, 0.03, dir.z)), sunDir,
                             frame.sky2.x, frame.sky2.y, frame.sky2.z, frame.sky2.w, false)
                 * frame.skyRef.x;
  color = mix(color, horizon, clamp(aerial, 0.0, 0.80));

  // Soft opacity: thin edges let the sky through, cores are nearly solid.
  float opacity = smoothstep(0.0, 0.55, mask) * (0.35 + 0.65 * depth);

  return vec4(color * frame.skyRef.x * 1.25, clamp(opacity, 0.0, 0.97));
}

// Visible sky: Preetham atmosphere + sun disc + cloud deck.
vec3 skyWithClouds(vec3 dir) {
  vec3 sunDir = normalize(frame.lightDir.xyz);
  float coverage = frame.skyRef.w;
  float time = frame.camPos.w;
  float wind = frame.weather.z;
  float rain = clamp(frame.weather.x, 0.0, 1.0);

  // Overcast: rain fills the sky with cloud without the user touching the
  // coverage slider, so a downpour never happens in a clear blue sky.
  float cov = clamp(coverage + rain * 0.85, 0.0, 1.0);

  vec3 color = preethamSky(dir, sunDir, frame.sky2.x, frame.sky2.y, frame.sky2.z,
                           frame.sky2.w, true) * frame.skyRef.x;

  vec4 clouds = cloudDeck(dir, sunDir, cov, time, wind);
  color = color * (1.0 - clouds.a) + clouds.rgb;

  // Rain dims and desaturates the whole sky (heavy water column scattering).
  if (rain > 0.001) {
    vec3 grey = vec3(luma(color)) * vec3(0.86, 0.89, 0.96);
    color = mix(color, grey * 0.72, rain * 0.55);
  }
  return color;
}

// Rain bands in the air: a cheap screen-space veil that reads as sheets of
// water between the camera and the scene. Returns rgb veil and alpha.
vec4 rainVeil(vec3 dir) {
  float rain = clamp(frame.weather.x, 0.0, 1.0);
  if (rain <= 0.001) return vec4(0.0);
  float t = frame.weather2.w;
  // Low, wide noise bands: layers of falling water drifting with the wind.
  vec2 uv = vec2(atan(dir.z, dir.x) * 0.5, dir.y * 0.9);
  float n = gradNoise(uv * vec2(9.0, 5.0) + vec2(t * 0.35, -t * 1.6));
  n = smoothstep(0.45, 0.85, n);
  // Only between the horizon and a few degrees up: real rain sheets are wide
  // and low, not overhead.
  n *= smoothstep(0.0, 0.08, dir.y) * (1.0 - smoothstep(0.35, 0.9, dir.y));
  float a = n * rain * 0.16;
  vec3 tint = mix(vec3(0.62, 0.66, 0.72), vec3(0.78, 0.80, 0.84),
                  smoothstep(0.0, 0.4, dir.y));
  return vec4(tint, a);
}

#endif
