// head.frag - skin shading: wrapped diffuse + GGX specular picking up the hair
// transmittance shadow, plus sky ambient and a little subsurface tint.
#version 450

#include "lib/common.glsl"
#include "lib/shadow.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vViewPos;
layout(location = 3) in vec3 vLightPos;
layout(location = 4) in vec2 vUV;

layout(location = 0) out vec4 outColor;

float distributionGGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-6);
}

void main() {
  vec3 n = normalize(vNormal);
  vec3 w_o = normalize(frame.camPos.xyz - vWorld);
  if (dot(n, w_o) < 0.0) n = -n;  // the mesh is single sided in places

  vec3 albedo = texture(skinAlbedo, vUV).rgb;
  // Slight desaturation of the albedo photo keeps it from looking pasted on.
  albedo = mix(albedo, vec3(luma(albedo)), 0.12);
  albedo = pow(max(albedo, vec3(0.0)), vec3(2.4));

  vec3 sunDir = normalize(frame.lightDir.xyz);
  float NoL = dot(n, sunDir);
  float ndl = max(NoL, 0.0);

  float bias = 0.005 * sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.08) + 0.0006;
  float directF;
  vec3 spread;
  vec3 trans = shadowTransmittance(spread, directF, vLightPos, bias);

  vec3 sunColor = vec3(1.0, 0.95, 0.89) * frame.skyRef.x * 1.5;

  // Wrapped diffuse: light that travelled through the hair arrives softer.
  float wrap = clamp((NoL + 0.25) / 1.25, 0.0, 1.0);
  vec3 diffuse = sunColor * mix(vec3(ndl), trans * wrap, 0.55) * albedo;

  // Specular (skin F0 ~ 0.028) with the hair shadow applied.
  vec3 h = normalize(w_o + sunDir);
  float NoH = max(dot(n, h), 0.0);
  float VoH = max(dot(w_o, h), 0.0);
  float a = 0.42;
  float F = 0.028 + 0.972 * pow(1.0 - VoH, 5.0);
  float spec = distributionGGX(NoH, a) * F * ndl * 0.16;
  vec3 specular = sunColor * spec * mix(1.0, directF, 0.9);

  // Ambient / IBL
  vec3 ambient = skyAmbient(n, 0.6) * albedo * 0.30;
  // Fake subsurface: skylight through the ears/nose edges.
  float rim = pow(1.0 - max(dot(n, w_o), 0.0), 2.5);
  vec3 sss = sunColor * rim * albedo * 0.03 * (0.3 + 0.7 * clamp(NoL * 0.5 + 0.5, 0.0, 1.0));

  vec3 color = diffuse + specular + ambient + sss + spread * sunColor * 0.02;
  outColor = vec4(color, 1.0);
}
