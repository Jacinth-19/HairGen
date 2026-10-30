// hair_shading.glsl - Marschner fiber BSDF with the demo's precomputed
// normalized absorption / azimuthal-scattering lookup tables.
#ifndef HGEN_HAIR_SHADING_GLSL
#define HGEN_HAIR_SHADING_GLSL

#define ONE_OVER_PI (1.0 / PI)
#define ONE_OVER_PI_HALF (2.0 / PI)
#define DEG2RAD(x) ((x) / 180.0 * PI)

#define SCALE_N_R 0.25
#define SCALE_N_TT 0.5
#define SCALE_N_TRT 0.25
#define SCALE_N_R_GI 0.0625
#define SCALE_N_TT_GI 0.66666666666666666666666666666667
#define SCALE_N_TRT_GI 0.03703703703703703703703703703704
#define SCALE_M_GI_R 2.6315789473684210526315789473684
#define SCALE_M_GI_TT 2.2222222222222222222222222222222
#define SCALE_M_GI_TRT 1.5151515151515151515151515151515
#define SCALE_GI 0.05
// The demo multiplies by 5 and takes a sqrt() for a display-referred target.
// Here the frame target is linear HDR, so the scale is folded into the
// exposure and the value stays in radiance units.
#define REAL_INTENSITY 1.35

float Rand(int n, int seed) {
  n = (n << 13) ^ (n + seed);
  n = (n * (n * n * 15731 + 789221) + 1376312589) + n;
  return float(n & 0x7fffffff) / 2147483648.0;
}

// ShadeHair(): pLight is the light-space position of the shaded point, tangent
// its view-space fiber tangent, w_o the view vector, instanceID the strand id.
// `directFraction` is handed back to the caller: hair.frag needs it for the
// Kajiya-Kay term, and the shadow lookup is by far the most expensive thing in
// this shader (~45 taps), so doing it twice per fragment was pure waste.
vec3 ShadeHair(vec3 pLight, vec3 tangent, vec3 w_o, int instanceID, out float directFraction) {
  vec3 spread;
  vec3 trans_direct = Shadow(spread, directFraction, pLight, 0.0);
  vec3 trans = trans_direct - directFraction;

  vec3 w_i = normalize(frame.lightDirView.xyz);

  // theta
  float sinTheta_i = dot(w_i, tangent);
  float theta_i = asin(clamp(sinTheta_i, -1.0, 1.0));
  float sinTheta_o = dot(w_o, tangent);
  float theta_o = asin(clamp(sinTheta_o, -1.0, 1.0));
  float theta_h = theta_o + theta_i;
  float theta = 0.5 * abs(theta_o - theta_i);

  // lookup coordinates for multiple scattering
  float ix_theta_h = theta_h * ONE_OVER_PI * 0.5 + 0.5;
  float ix_theta = theta * ONE_OVER_PI_HALF;
  vec3 ix_spread = sqrt(max(spread, vec3(0.0))) * ONE_OVER_PI * 0.5;

  // phi
  vec3 azL = normalize(w_i - sinTheta_i * tangent);
  vec3 azV = normalize(w_o - sinTheta_o * tangent);
  float cosPhiH = dot(azL, azV);
  float phiH = acos(clamp(cosPhiH, -1.0, 1.0));

  // factor for non-circular fibers
  float randVal = Rand(instanceID, 3);
  float phi_shift = DEG2RAD(25.0) * (randVal - 0.5);
  if (dot(tangent, cross(azL, azV)) < 0.0) phi_shift = -phi_shift;
  float phiTRT = abs(phiH + phi_shift);
  if (phiTRT > 2.0 * PI) phiTRT -= 2.0 * PI;
  if (phiTRT > PI) phiTRT = 2.0 * PI - phiTRT;

  // N for direct illumination
  vec2 index = vec2(phiH * ONE_OVER_PI, ix_theta);
  vec2 index2 = vec2(phiTRT * ONE_OVER_PI, index.y);

  vec4 NTTR = texture(N_TT_R, index);
  float NR = SCALE_N_R * NTTR.a;
  vec3 NTT = SCALE_N_TT * NTTR.rgb;
  vec3 NTRT = SCALE_N_TRT * texture(N_TRT, index2).rgb;

  // M for direct illumination
  float R_DEV = DEG2RAD(frame.fiber.w);
  float TT_DEV = DEG2RAD(frame.fiber2.x);
  float TRT_DEV = DEG2RAD(frame.fiber2.y);
  float R_SHIFT = DEG2RAD(-5.0);
  float TT_SHIFT = -R_SHIFT * 0.5;
  float TRT_SHIFT = -R_SHIFT * 1.5;

  float thetaR = theta_h - R_SHIFT;
  float MR = exp(-0.5 * thetaR * thetaR / (R_DEV * R_DEV)) / (sqrt(2.0 * PI) * R_DEV);
  float thetaTT = theta_h - TT_SHIFT;
  float MTT = exp(-0.5 * thetaTT * thetaTT / (TT_DEV * TT_DEV)) / (sqrt(2.0 * PI) * TT_DEV);
  float thetaTRT = theta_h - TRT_SHIFT;
  float MTRT = exp(-0.5 * thetaTRT * thetaTRT / (TRT_DEV * TRT_DEV)) / (sqrt(2.0 * PI) * TRT_DEV);

  // Specular (UE's Hair model input) scales the R lobe - the highlight that
  // carries the *light's* colour, as opposed to the fibre-tinted TT/TRT lobes.
  vec3 color = MR * NR * clamp(frame.ueHair.y, 0.0, 2.0) + MTT * NTT + MTRT * NTRT;

  // local scattering
  vec3 gi = vec3(0.0);
  float density = 0.7;
  gi.r = density * texture(GI, vec3(ix_spread.r, ix_theta_h, ix_theta)).r;
  gi.g = density * texture(GI, vec3(ix_spread.g, ix_theta_h, ix_theta)).g;
  gi.b = density * texture(GI, vec3(ix_spread.b, ix_theta_h, ix_theta)).b;

  // Scatter (UE's Hair model input) scales the dual-scattering component: the
  // light that entered the fibre cloud, bounced and left it again. 0.65 - UE's
  // default artistic value - is unity here, 1.0 lifts it by 35 %.
  const float kScatterScale = mix(0.35, 1.35, clamp(frame.ueHair.x, 0.0, 1.0));
  gi *= kScatterScale;
  color += gi;
  color *= directFraction;

  // global scattering
  vec4 nttr_gi = texture(N_TT_R_GI, index);
  float nr_gi = nttr_gi.a;
  vec3 ntt_gi = nttr_gi.rgb;
  vec3 ntrt_gi = texture(N_TRT_GI, index2).rgb;

  vec3 MGIr = texture(M_GI, vec2(ix_spread.r, ix_theta_h)).xyz;
  vec3 MGIg = texture(M_GI, vec2(ix_spread.g, ix_theta_h)).xyz;
  vec3 MGIb = texture(M_GI, vec2(ix_spread.b, ix_theta_h)).xyz;
  vec3 mr_gi = vec3(MGIr.x, MGIg.x, MGIb.x);
  vec3 mtt_gi = vec3(MGIr.y, MGIg.y, MGIb.y);
  vec3 mtrt_gi = vec3(MGIr.z, MGIg.z, MGIb.z);

  vec3 f_indir = trans * density * kScatterScale *
                 (gi + mr_gi * nr_gi + mtt_gi * ntt_gi + mtrt_gi * ntrt_gi);
  color += f_indir;

  color += gi * 0.1;

  return max(color, vec3(0.0)) * REAL_INTENSITY;
}

#endif
