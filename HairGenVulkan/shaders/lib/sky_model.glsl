// sky_model.glsl - Preetham et al. 1999 analytic daylight model.
//
// Port of the reference implementation shipped with three.js
// (`examples/jsm/objects/Sky.js`, the de-facto standard analytic skydome,
// based on "A Practical Analytic Model for Daylight"). It replaces the
// hand-rolled 24-step single-scattering integrator that used to live in
// sky_lut.frag: that one looked plausible but its absolute radiance depended on
// an arbitrary scale, so exposure had to be re-tuned per sun angle. Preetham has
// physically measured Rayleigh/Mie coefficients, a proper solar zenith
// intensity curve and a real extinction term, so the zenith goes blue, the
// horizon warms and the sun sets red on its own.
//
// Parameters (frame.sky2): turbidity, rayleigh, mie coefficient, mie anisotropy.
#ifndef HGEN_SKY_MODEL_GLSL
#define HGEN_SKY_MODEL_GLSL

const float kSunAngularDiameterCos = 0.9999567;   // 66 arc seconds
const float kRayleighZenithLength = 8.4e3;        // optical length at zenith
const float kMieZenithLength = 1.25e3;
const float kThreeOverSixteenPi = 0.05968310365946075;
const float kOneOverFourPi = 0.07957747154594767;

// Precomputed for the three primaries (680, 550, 450 nm).
const vec3 kTotalRayleigh = vec3(5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5);
const vec3 kMieConst = vec3(1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14);

vec3 totalMie(float turbidity) {
  const float c = (0.2 * 1.0) * 10e-18;
  return 0.434 * c * kMieConst * turbidity;
}

float sunIntensity(float zenithAngleCos) {
  // Earth-shadow hack: fades the sun out as it sets, which is what produces the
  // sunset colour ramp instead of a hard terminator.
  const float cutoffAngle = 1.6110731556870734;
  const float steepness = 1.5;
  const float ee = 1000.0;
  zenithAngleCos = clamp(zenithAngleCos, -1.0, 1.0);
  return ee * max(0.0, 1.0 - pow(2.718281828459045, -((cutoffAngle - acos(zenithAngleCos)) / steepness)));
}

float rayleighPhase(float cosTheta) {
  return kThreeOverSixteenPi * (1.0 + cosTheta * cosTheta);
}

float hgPhase(float cosTheta, float g) {
  float g2 = g * g;
  float inv = 1.0 / pow(max(1.0 - 2.0 * g * cosTheta + g2, 1e-4), 1.5);
  return kOneOverFourPi * ((1.0 - g2) * inv);
}

// Sky radiance (linear HDR, roughly in the same units as the rest of the frame
// after the gain below) for a view direction and a unit sun direction.
// `withSun` adds the solar disc; the ambient bake leaves it out.
vec3 preethamSky(vec3 dir, vec3 sunDir, float turbidity, float rayleigh,
                 float mieCoefficient, float mieG, bool withSun) {
  sunDir = normalize(sunDir);
  dir = normalize(dir);

  float sunE = sunIntensity(sunDir.y);

  // Rayleigh and Mie coefficients, with Preetham's sunset fade.
  float sunfade = 1.0 - clamp(1.0 - exp(sunDir.y / 450000.0), 0.0, 1.0);
  float rayleighCoefficient = max(rayleigh - (1.0 * (1.0 - sunfade)), 0.01);
  vec3 betaR = kTotalRayleigh * rayleighCoefficient;
  vec3 betaM = totalMie(turbidity) * mieCoefficient;

  // Optical depth along the view ray.
  float zenithAngle = acos(max(0.0, dir.y));
  float inv = 1.0 / (cos(zenithAngle) + 0.15 * pow(93.885 - (zenithAngle * 180.0 / PI), -1.253));
  float sR = kRayleighZenithLength * inv;
  float sM = kMieZenithLength * inv;

  vec3 fex = exp(-(betaR * sR + betaM * sM));

  float cosTheta = dot(dir, sunDir);
  vec3 betaRTheta = betaR * rayleighPhase(cosTheta * 0.5 + 0.5);
  vec3 betaMTheta = betaM * hgPhase(cosTheta, mieG);

  vec3 lin = pow(max(sunE * ((betaRTheta + betaMTheta) / (betaR + betaM)) * (1.0 - fex), vec3(0.0)),
                 vec3(1.5));
  lin *= mix(vec3(1.0),
             pow(max(sunE * ((betaRTheta + betaMTheta) / (betaR + betaM)) * fex, vec3(0.0)),
                 vec3(0.5)),
             clamp(pow(1.0 - sunDir.y, 5.0), 0.0, 1.0));

  // The model is calibrated so that 0.05 * radiance is a correctly exposed
  // image; that 0.04-0.05 factor is the model's own "exposure". Our frame is
  // linear HDR and tone-mapped later, so only that factor is applied here and
  // the ACES curve in post.frag does the rest. 1.0 = a sunlit white surface.
  vec3 color = (lin + vec3(0.1) * fex) * 0.012;
  // Below the horizon the model has no meaning (the optical path is clamped at
  // 90 degrees, which makes the ground plane glow). Dim it: the floor pass
  // covers it anyway, and the horizon haze that samples this should not be
  // white.
  color *= mix(0.28, 1.0, smoothstep(-0.06, 0.02, dir.y));

  if (withSun) {
    // Solar disc: ~0.5 degrees, clipped to a sane HDR value. It is meant to
    // blow out to white through the tone mapper and spill into the bloom pass.
    float disc = clamp((cosTheta - kSunAngularDiameterCos) * 40000.0, 0.0, 1.0);
    color += vec3(11.0) * disc * clamp(sunE * fex * 0.042, vec3(0.0), vec3(1.0));
  }
  return max(color, vec3(0.0));
}

vec3 preethamSky(vec3 dir, vec3 sunDir) {
  return preethamSky(dir, sunDir, frame.sky2.x, frame.sky2.y, frame.sky2.z, frame.sky2.w, true);
}

#endif
