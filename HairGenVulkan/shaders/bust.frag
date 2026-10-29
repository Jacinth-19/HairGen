// bust.frag - the pedestal under the neck: a matte dark material so the
// portrait sits on something instead of ending in the torn shoulder mesh.
#version 450

#include "lib/common.glsl"
#include "lib/shadow.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vViewPos;
layout(location = 3) in vec3 vLightPos;
layout(location = 4) in vec2 vUV;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  vec3 w_o = normalize(frame.camPos.xyz - vWorld);
  vec3 sunDir = normalize(frame.lightDir.xyz);
  float ndl = max(dot(n, sunDir), 0.0);

  float slope = sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.15);
  vec3 receivePos = vWorld + n * (0.004 + 0.012 * slope);
  vec3 pLight = (frame.lightView * vec4(receivePos, 1.0)).xyz;
  float bias = 0.0025 + 0.005 * slope;
  float directF;
  vec3 spread;
  vec3 trans = shadowTransmittance(spread, directF, pLight, bias);

  // Polished dark stone rather than matte black: it picks up the sky and the
  // key light so the bust reads as sitting on something, not floating.
  const vec3 albedo = vec3(0.052, 0.054, 0.060);
  vec3 sunColor = vec3(1.0, 0.95, 0.89) * frame.skyRef.x * 1.5;

  // Direct + the hair shadow that falls across the pedestal.
  vec3 direct = sunColor * mix(vec3(ndl), trans * ndl, 0.85);
  // Sky fill plus a little bounce from the floor.
  vec3 ambient = skyAmbient(n, 0.9) * 0.30 + vec3(0.01);

  // Soft sheen so the silhouette reads against the background.
  vec3 h = normalize(w_o + sunDir);
  float sheen = pow(max(dot(n, h), 0.0), 48.0) * 0.12;
  float rim = pow(1.0 - max(dot(n, w_o), 0.0), 2.5) * 0.10;

  vec3 color = (direct + ambient) * albedo + spread * sunColor * 0.05 + sheen + rim * albedo * 8.0;
  outColor = vec4(color, 1.0);
}
