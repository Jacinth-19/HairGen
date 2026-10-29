// ground.frag - infinite procedural ground plane that catches the head and hair
// shadows (the demo has no floor at all). Ray-traced against y = groundY inside a
// fullscreen pass so the horizon is exact and needs no tessellation.
#version 450

#include "lib/common.glsl"
#include "lib/shadow.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 viewRay(vec2 uv) {
  vec4 p = frame.invView * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  return normalize(p.xyz / p.w - frame.camPos.xyz);
}

// Slightly wavy matte studio floor.
float groundAlbedo(vec2 xz) {
  float n = texture(tex_noise, vec3(xz * 0.08, 0.5)).r;
  float n2 = texture(tex_noise, vec3(xz * 0.31, 0.2)).g;
  return 0.26 + 0.05 * n + 0.02 * n2;
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

  float albedo = groundAlbedo(world.xz);

  vec3 pLight = (frame.lightView * vec4(world, 1.0)).xyz;
  float tanT = 0.0;  // floor normal is perpendicular to the sun; keep a fixed bias
  float bias = 0.0015;
  float directF;
  vec3 spread;
  vec3 trans = shadowTransmittance(spread, directF, pLight, bias);
  // Only inside the shadow map footprint do the hair/object shadows apply.
  vec2 shadUV = pLight.xy * 0.5 + 0.5;
  float inside = smoothstep(0.0, 0.06, min(min(shadUV.x, 1.0 - shadUV.x), min(shadUV.y, 1.0 - shadUV.y)));
  float ndl = max(dot(vec3(0.0, 1.0, 0.0), normalize(frame.lightDir.xyz)), 0.0);
  vec3 sunColor = vec3(1.0, 0.95, 0.88) * frame.skyRef.x * 1.7;

  vec3 direct = sunColor * ndl * mix(vec3(1.0), trans, inside);
  vec3 ambient = skyAmbient(vec3(0.0, 1.0, 0.0), 1.0) * 0.45;

  // A little hair-colored light bleeding onto the floor under the shadow.
  vec3 bounce = spread * sunColor * inside * 0.6;

  vec3 color = (direct + ambient) * albedo + bounce * 0.5;

  // Distance haze towards the horizon colour.
  float haze = 1.0 - exp(-t * 0.06);
  vec3 horizon = skyRadiance(normalize(vec3(dir.x, abs(dir.y) * 0.15 + 0.02, dir.z)));
  color = mix(color, horizon * 0.85, haze);

  outColor = vec4(color, 1.0);
}
