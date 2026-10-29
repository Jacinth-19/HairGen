// rain.frag - the streak itself: a thin, slightly refractive water thread.
//
// Rain is mostly transparent seen edge-on and brightens where it catches the
// sun, so the streak is shaded as a rim-lit glass rod: dark core, bright edge,
// plus a specular glint on the side facing the light.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vFade;
layout(location = 2) in float vDepth;
layout(location = 0) out vec4 outColor;

void main() {
  float across = vUV.x * 2.0 - 1.0;      // -1 .. 1
  float along = vUV.y;

  // Round rod profile: bright thin edges, transparent centre.
  float edge = smoothstep(0.35, 1.0, abs(across));
  float core = 1.0 - abs(across);
  core = pow(core, 1.5);

  // Taper to points at both ends.
  float taper = smoothstep(0.0, 0.18, along) * smoothstep(1.0, 0.72, along);

  // Cheap pseudo-normal of the rod, tilted toward the camera side.
  vec3 glint = vec3(1.0, 0.97, 0.92) * pow(edge, 3.0) * 2.2;

  vec3 base = vec3(0.46, 0.50, 0.58);
  vec3 color = base * (0.40 + 0.60 * core) + glint * (0.12 + 0.22 * frame.weather.x);

  // Distant drops get misty: blend toward the hazy sky.
  float mist = smoothstep(4.0, 14.0, vDepth);
  color = mix(color, base * 1.35, mist);

  float alpha = (0.06 + 0.32 * core * 0.55 + 0.42 * edge) * taper * vFade;
  alpha *= clamp(0.35 + frame.weather.x * 1.1, 0.0, 1.0);
  alpha *= 1.0 - 0.65 * mist;

  // No discard: with MSAA + blending the driver is much happier with a
  // zero-alpha write than with a per-sample kill, and the blend makes it a no-op.
  outColor = vec4(max(color, vec3(0.0)), clamp(alpha, 0.0, 1.0));
}
