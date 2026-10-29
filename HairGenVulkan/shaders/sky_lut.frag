// sky_lut.frag - Preetham (1999) analytic daylight baked into a small
// equirectangular table every frame. The table is both the ambient/IBL source
// for the head, hair and ground; the visible background adds the solar disc and
// the cloud deck on top (background.frag), so nothing high frequency lands in
// the irradiance probe.
#version 450

#include "lib/common.glsl"
#include "lib/sky_model.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// The atmosphere itself lives in lib/sky_model.glsl (Preetham 1999).

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

  // No sun disc here: this table doubles as the irradiance probe that
  // skyAmbient() averages, and a 0.5 degree disc in a 256x128 table is a
  // handful of texels of pure white that blow out every ambient lookup.
  vec3 color = preethamSky(dir, sunDir, frame.sky2.x, frame.sky2.y, frame.sky2.z,
                           frame.sky2.w, false);

  // Exposure control: the existing "sun intensity" slider is a global gain on
  // the atmosphere so the look can be pushed without touching the tone mapper.
  color *= frame.skyRef.x;

  // A little extra saturation keeps the hair separated from the background;
  // Preetham is accurate but reads slightly desaturated after the tone curve.
  color = mix(vec3(luma(color)), color, 1.20);

  // Keep the table smooth: high frequency content here becomes blotches on the
  // head through skyAmbient(). Clouds are composited in background.frag.
  outColor = vec4(max(color, vec3(0.0)), 1.0);
}
