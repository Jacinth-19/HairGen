// common.glsl - frame uniforms + helpers shared by every pass.
#ifndef HGEN_COMMON_GLSL
#define HGEN_COMMON_GLSL

#define PI 3.14159265358979323846

layout(set = 0, binding = 0) uniform FrameBlock {
  mat4 mvp;          // camera view-projection
  mat4 mv;           // camera view
  mat4 lightMVP;     // world -> light clip
  mat4 lightView;    // world -> light view (rotation * scale)
  mat4 invView;
  vec4 camPos;       // xyz = world camera position, w = frame time (s)
  vec4 lightDir;     // xyz = unit vector towards the sun
  vec4 resolution;   // xy = pixel size, zw = 1/size
  vec4 lightDirView; // sun direction in view space
  vec4 hair0;        // hairDensity, lengthVar, fuzzAmount, fuzzDistrib
  vec4 hair1;        // fuzzExp, frizzAmount, frizzFreq, frizzExp
  vec4 hair2;        // kinkAmount, kinkFreqUV, kinkFreqW, kinkWScale
  vec4 hair3;        // kinkExp, clumpPercent, clumpSize, clumpExp
  vec4 hair4;        // clumpVar, clumpNoise, clumpFreq, curlAmount
  vec4 hair5;        // curlFreq, curlTwists, curlTwistExp, curlExp
  vec4 fiber;        // a_f (rgb), R_dev (deg)
  vec4 fiber2;       // TT_dev, TRT_dev, density, ambientStrength
  vec4 skyRef;       // sunIntensity, exposure, bloom, cloudCoverage
  vec4 misc;         // groundY, showGround, debug view id, frame index
  vec4 dye;          // xyz = dye colour (linear), w = dye strength
  vec4 dye2;         // rootDarken, perStrandVariation, sheen, unused
  vec4 sky2;         // turbidity, rayleigh, mie, mieG  (Preetham atmosphere)
  vec4 weather;      // rain, wetness, wind strength, wind azimuth
  vec4 weather2;     // hair gravity, puddles, lens droplets, animation time
} frame;

layout(set = 0, binding = 1) uniform sampler2DShadow objShadMap;
layout(set = 0, binding = 2) uniform sampler2D hairShadMap;  // raw depth, manual PCF
layout(set = 0, binding = 3) uniform sampler2D skyLUT;
layout(set = 0, binding = 4) uniform sampler2D bloomTex;
layout(set = 0, binding = 5) uniform sampler2D hdrTex;  // main HDR target (post passes)
layout(set = 0, binding = 6) uniform sampler2D skinFlat;   // low-frequency skin albedo
layout(set = 0, binding = 7) uniform sampler2D skinNormal; // tangent-space detail normal
layout(set = 0, binding = 8) uniform sampler2D skinAO;     // ambient occlusion mask
layout(set = 0, binding = 9) uniform sampler2D skinSpec;   // specular mask

layout(set = 1, binding = 0) uniform sampler3D tex_hairmesh;
layout(set = 1, binding = 1) uniform sampler2D tex_uv;
layout(set = 1, binding = 2) uniform sampler3D tex_uw;
layout(set = 1, binding = 3) uniform sampler3D tex_v;
layout(set = 1, binding = 4) uniform sampler3D tex_noise;
layout(set = 1, binding = 5) uniform sampler2D tex_clump;
layout(set = 1, binding = 6) uniform sampler2D N_TT_R;
layout(set = 1, binding = 7) uniform sampler2D N_TRT;
layout(set = 1, binding = 8) uniform sampler2D N_TT_R_GI;
layout(set = 1, binding = 9) uniform sampler2D N_TRT_GI;
layout(set = 1, binding = 10) uniform sampler3D GI;
layout(set = 1, binding = 11) uniform sampler2D M_GI;
layout(set = 1, binding = 12) uniform sampler2D skinAlbedo;

vec2 equirectUV(vec3 d) {
  return vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
}

vec3 skyRadiance(vec3 d) { return texture(skyLUT, equirectUV(normalize(d))).rgb; }

// Irradiance lookup. The LUT is dimmed below the horizon (the Preetham model
// has no meaning there), so a direction that grazes y = 0 steps between bright
// haze and the dimmed floor. Feeding that step into the ambient probe drew a
// hard horizontal band across everything the probe lights -- most visibly the
// hair, which samples the probe with near-horizontal directions along its
// whole length. Lift the sample direction off the horizon so the probe sees a
// continuous sky.
vec3 skyProbe(vec3 d) {
  d = normalize(d);
  d.y = max(d.y, 0.10);
  return skyRadiance(normalize(d));
}

// Cosine weighted average of the sky over the hemisphere around n: cheap IBL.
vec3 skyAmbient(vec3 n, float roughness) {
  vec3 up = normalize(n);
  vec3 t1 = normalize(cross(abs(up.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), up));
  vec3 t2 = cross(up, t1);
  float spread = 0.35 + 0.9 * roughness;
  vec3 acc = skyProbe(up) * 1.4;
  acc += skyProbe(up + t1 * spread) * 0.55;
  acc += skyProbe(up - t1 * spread) * 0.55;
  acc += skyProbe(up + t2 * spread) * 0.55;
  acc += skyProbe(up - t2 * spread) * 0.55;
  acc += skyProbe(up * 0.4 + normalize(t1 + t2) * spread) * 0.35;
  return acc / 3.95;
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// ACES filmic approximation (Narkowicz) with a slight shoulder extension.
vec3 tonemapACES(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linearToSRGB(vec3 c) {
  c = max(c, vec3(0.0));
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

#endif
