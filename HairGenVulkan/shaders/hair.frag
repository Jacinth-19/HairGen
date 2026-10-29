// hair.frag - Marschner fiber shading with the demo's lookup tables, hair shadow
// transmittance and a sky ambient fill so lit-by-sky strands are not black.
#version 450

#include "lib/common.glsl"
#include "lib/shadow.glsl"
#include "lib/hair_shading.glsl"

layout(location = 0) in vec4 vViewPos;
layout(location = 1) in vec3 vTangentView;
layout(location = 2) in vec3 vLightPos;
layout(location = 3) in vec3 vWorld;
layout(location = 4) in float vW;
layout(location = 5) flat in float vWMax;
layout(location = 6) flat in int vInstance;

layout(location = 0) out vec4 outColor;

void main() {
  if (vW > vWMax) discard;

  vec3 v = normalize(-vViewPos.xyz / vViewPos.w);
  vec3 t = normalize(vTangentView);

  vec3 color = ShadeHair(vLightPos, t, v, vInstance);

  // Ambient: skylight picked up by the fiber, weighted towards the root so the
  // tips stay light and airy.
  vec3 nApprox = normalize(cross(t, v));
  vec3 ambient = skyAmbient(nApprox, 0.4) * frame.fiber.rgb * frame.fiber2.w;
  // Roots are buried in the hair mass and see less of the sky.
  float occlusion = mix(0.25, 1.0, smoothstep(0.0, 0.85, vW));
  color += ambient * occlusion;

  outColor = vec4(max(color, vec3(0.0)), 1.0);
}
