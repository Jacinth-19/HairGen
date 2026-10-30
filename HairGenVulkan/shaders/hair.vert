// hair.vert - hair-mesh strand placement and styling.
//
// Port of EvalHairVert() from the original demo: each instanced strip walks the
// hair-mesh 3D texture, applies clump / kink / curl / frizz / fuzz displacement
// in the hair-mesh surface frame, and expands the ribbon in screen space.
//
// Changes over the demo, all of them fixes for visible defects:
//   * the two root-frames are sanitised, so a degenerate texel can no longer
//     produce a NaN tangent (which drew black wedges and vanished ribbons);
//   * the clump target of the *second* vertex now uses its own delta (the demo
//     re-used the first vertex's, which bent each ribbon segment);
//   * the clump distance is clamped before the divide, so clumpSize = 0 with a
//     zero-length delta no longer yields 0/0;
//   * the shadow pass expands the ribbon too. Without that every strand
//     collapses to a zero-area quad in the depth map, so the hair shadow map
//     was empty and the whole mass rendered fully lit - the single biggest
//     reason it read as flat;
//   * `cut_length` shortens every strand along its own parameter and `volume`
//     lifts the mass away from the skull, which is how the style presets get
//     their silhouette out of one fixed guide mesh.
#version 450

#include "lib/common.glsl"

layout(location = 0) in vec4 inPos;    // (w0 index, w1 index, mix, w)
layout(location = 1) in vec4 inPos2;   // neighbour sample used for the tangent
layout(location = 2) in vec3 inTexW;   // (w for tex_uw, w2, +-ribbon width)

layout(push_constant) uniform HairPC {
  vec4 st;  // st.xy = bundle texel, st.z = ribbon half width, st.w = shadow pass flag
  // st2.x = first root sample for this bundle,
  // st2.y = 1 -> use the explicit root table (july_rootsamples.js),
  // st2.z = 1 -> collide strands against the head colliders (july_colliders.js),
  // st2.w = number of roots this bundle owns (table wrapping).
  vec4 st2;
} pc;

// Explicit root distribution exported from july_rootsamples.js: (s, t) pairs
// grouped per bundle, indexed by an exclusive prefix table on the CPU.
layout(std430, set = 1, binding = 13) readonly buffer RootSamples { vec2 roots[]; };

// july_colliders.js: each entry is a pair of spheres (xyz, r) forming a
// capsule-ish approximation of the head and neck.
layout(std430, set = 1, binding = 14) readonly buffer Colliders { vec4 collider[]; };

layout(location = 0) out vec4 vViewPos;
layout(location = 1) out vec3 vTangentView;
layout(location = 2) out vec3 vLightPos;
layout(location = 3) out vec3 vWorld;
layout(location = 4) out float vW;
layout(location = 5) flat out float vWMax;
layout(location = 6) flat out int vInstance;
layout(location = 7) out float vAlpha;   // coverage, used with alpha-to-coverage

float Rand(int n, int seed) {
  n = (n << 13) ^ (n + seed);
  n = (n * (n * n * 15731 + 789221) + 1376312589) + n;
  return float(n & 0x7fffffff) / 2147483648.0;
}

vec3 getPosFromHairMesh(vec2 stp, vec3 pos) {
  vec3 p0 = texture(tex_hairmesh, vec3(stp, pos.x)).rgb;
  vec3 p1 = texture(tex_hairmesh, vec3(stp, pos.y)).rgb;
  return mix(p0, p1, pos.z);
}

vec3 noise3(vec3 tc) { return texture(tex_noise, tc).rgb * 2.0 - 1.0; }

// A unit vector, or `fallback` when the input is (numerically) zero. The demo
// normalised unconditionally; a single bad texel was enough to put NaN into the
// vertex position and paint a black shard across the frame.
vec3 safeDir(vec3 v, vec3 fallback) {
  float l2 = dot(v, v);
  return l2 > 1e-10 ? v * inversesqrt(l2) : fallback;
}

// Push a hair vertex out of the head colliders. The demo runs this in its
// physics solver; a dense strand render can apply it directly in the vertex
// shader because the geometry is static.
vec3 collideWithHead(vec3 p) {
  if (pc.st2.z < 0.5) return p;
  int n = int(collider.length());
  for (int i = 0; i + 1 < n; i += 2) {
    for (int k = 0; k < 2; ++k) {
      vec4 c = collider[i + k];
      if (c.w <= 0.0) continue;
      vec3 d = p - c.xyz;
      float dist = length(d);
      if (dist < c.w && dist > 1e-6) p += d * ((c.w - dist) / dist);
    }
  }
  return p;
}

// Head centre, used as the origin of the volume lift. The collider list starts
// at the skull, so use it when the demo's data is present.
vec3 headCentre() {
  if (collider.length() > 0 && collider[0].w > 0.0) return collider[0].xyz;
  return vec3(0.0, 0.05, 0.0);
}

float evalHairVert(out vec3 p, out vec3 p2, vec4 pos, vec4 pos2, vec2 texW) {
  ivec3 size = textureSize(tex_hairmesh, 0);
  int instance = gl_InstanceIndex;
  // Root: either the demo's explicit root table or the procedural jitter.
  // The table holds one root per strip in the demo; for denser renders the
  // roots are reused cyclically with a small per-strand offset.
  vec2 jitter = vec2(Rand(instance, 0), Rand(instance, 1));
  vec2 rootST = jitter;
  if (pc.st2.y > 0.5) {
    int n = max(1, int(pc.st2.w));
    rootST = roots[int(pc.st2.x) + instance % n] + jitter * 0.06;
  }
  vec2 stp = (pc.st.xy + rootST) / vec2(size.xy);

  float lengthVar = frame.hair0.y;
  float fuzzAmount = frame.hair0.z;
  float fuzzDistrib = frame.hair0.w;
  float fuzzExp = frame.hair1.x;
  float frizzAmount = frame.hair1.y;
  float frizzFreq = frame.hair1.z;
  float frizzExp = frame.hair1.w;
  float kinkAmount = frame.hair2.x;
  float kinkFreqUV = frame.hair2.y;
  float kinkFreqW = frame.hair2.z;
  float kinkWScale = frame.hair2.w;
  float kinkExp = frame.hair3.x;
  float clumpPercent = frame.hair3.y;
  float clumpSize = frame.hair3.z;
  float clumpExp = frame.hair3.w;
  float clumpVar = frame.hair4.x;
  float clumpNoise = frame.hair4.y;
  float clumpFreq = frame.hair4.z;
  float curlAmount = frame.hair4.w;
  float curlFreq = frame.hair5.x;
  float curlTwists = frame.hair5.y;
  float curlTwistExp = frame.hair5.z;
  float curlExp = frame.hair5.w;

  vec2 uv = texture(tex_uv, stp).rg;
  vec4 uw = texture(tex_uw, vec3(stp, texW.x));
  vec3 vDirRaw = texture(tex_v, vec3(stp, texW.x)).rgb;
  // The surface frame of the hair mesh. Both directions are sanitised: the
  // volume is filtered trilinearly, and near the bundle borders the two
  // directions can become parallel (a zero cross product = zero normal).
  vec3 uDir = safeDir(uw.xyz, vec3(0.0, 1.0, 0.0));
  vec3 vDir = safeDir(vDirRaw, vec3(1.0, 0.0, 0.0));
  vec3 wDir = cross(uDir, vDir);
  if (dot(wDir, wDir) < 1e-8) {
    // Parallel frame: rebuild an orthogonal basis around uDir.
    vDir = safeDir(cross(uDir, vec3(0.0, 0.0, 1.0)), vec3(1.0, 0.0, 0.0));
    wDir = cross(uDir, vDir);
  }
  wDir = safeDir(wDir, vec3(0.0, 0.0, 1.0));
  mat3 style2obj;
  style2obj[0] = vDir;
  style2obj[1] = uDir;
  style2obj[2] = wDir;

  float w = uw.w;
  float w2 = uw.w + pos2.w;

  p = getPosFromHairMesh(stp, pos.xyz);
  p2 = getPosFromHairMesh(stp, pos2.xyz);

  // Clump
  vec2 cuv = uv;
  if (Rand(instance, 2) < clumpPercent) {
    ivec2 ctsize = textureSize(tex_clump, 0);
    vec2 duv = clumpNoise * noise3(vec3(uv * clumpFreq, 0.0)).xy / vec2(ctsize);
    vec4 clumpPP = texture(tex_clump, uv + duv);
    vec2 stc = clamp(clumpPP.xy + clumpPP.zw / 256.0, vec2(0.0), vec2(1.0));
    float cDist = length(stc - stp);
    vec3 cp = getPosFromHairMesh(stc, pos.xyz);
    vec3 cp2 = getPosFromHairMesh(stc, pos2.xyz);
    vec3 cDiff = cp - p;
    vec3 cDiff2 = cp2 - p2;
    // Both deltas are clamped: with clumpSize = 0 (the demo's default) the
    // factor is only meaningful while the lengths are non-zero.
    float cLen = max(length(cDiff), 1e-6);
    float cLen2 = max(length(cDiff2), 1e-6);
    p += cDiff * (1.0 - cDist * clumpSize / cLen) * pow(w, clumpExp);
    p2 += cDiff2 * (1.0 - cDist * clumpSize / cLen2) * pow(w2, clumpExp);
    cuv = texture(tex_uv, stc).rg;
    uv = mix(cuv, uv, clumpVar);
  }

  // Kink
  vec3 dStyle = noise3(vec3(uv * kinkFreqUV, w * kinkFreqW));
  vec3 dStyle2 = noise3(vec3(uv * kinkFreqUV, w2 * kinkFreqW));
  dStyle *= kinkAmount * pow(w, kinkExp);
  dStyle2 *= kinkAmount * pow(w2, kinkExp);
  dStyle.z *= kinkWScale;
  dStyle2.z *= kinkWScale;

  // Curl
  float curlNoise = noise3(vec3(cuv * curlFreq, 0.0)).z;
  float curlAngle = curlTwists * (pow(w, curlTwistExp) + curlNoise);
  float curlAngle2 = curlTwists * (pow(w2, curlTwistExp) + curlNoise);
  dStyle.xy += vec2(cos(curlAngle), sin(curlAngle)) * curlAmount * pow(w, curlExp);
  dStyle2.xy += vec2(cos(curlAngle2), sin(curlAngle2)) * curlAmount * pow(w2, curlExp);

  // Frizz
  vec2 frizz = noise3(vec3(uv * frizzFreq, 0.0)).rg;
  dStyle.xy += frizz * (frizzAmount * pow(w, frizzExp));
  dStyle2.xy += frizz * (frizzAmount * pow(w2, frizzExp));

  // Fuzz
  float phi = (2.0 * PI) * Rand(instance, 3);
  float s = fuzzAmount * pow(Rand(instance, 4), fuzzDistrib);
  vec2 fuzz = vec2(cos(phi), sin(phi)) * s;
  dStyle.xy += fuzz * pow(w, fuzzExp);
  dStyle2.xy += fuzz * pow(w2, fuzzExp);

  p += style2obj * dStyle;
  p2 += style2obj * dStyle2;

  // ---- silhouette: volume ----------------------------------------------
  // Lift the mass off the skull. This is what separates a big, roundy style
  // from a close crop: the lift grows towards the tip (pow(w)) so the roots
  // stay anchored on the scalp and the silhouette expands outwards.
  float volume = frame.hair6.y;
  if (volume > 0.001) {
    vec3 centre = headCentre();
    vec3 radial = p - centre;
    float rl = max(length(radial), 1e-4);
    float lift = volume * 0.20 * pow(clamp(w, 0.0, 1.0), 1.25);
    p += radial * (lift / rl);
    vec3 radial2 = p2 - centre;
    p2 += radial2 * (lift / max(length(radial2), 1e-4));
  }

  // ---- physics: wind, gravity and wet weight ---------------------------
  // A strand behaves like a damped, tapered pendulum pinned at the root: the
  // further from the root, the more it moves (mass ~ w^1.6) and the slower it
  // returns. There is no spring integration here - the geometry is static - so
  // the dynamics are an analytic sum of two travelling waves plus a gust term,
  // which is enough to read as wind once the camera or the clock moves.
  {
    float windAmt = clamp(frame.weather.z, 0.0, 2.0);
    float gravAmt = clamp(frame.weather2.x, 0.0, 1.5);
    float wetAmt = clamp(frame.weather.y, 0.0, 1.0);
    float t = frame.weather2.w;

    float mass = pow(clamp(w, 0.0, 1.0), 1.7);
    float phase = Rand(instance, 8) * 6.2831853;
    // Two octaves of sway; the gust term makes whole clumps move together.
    float sway = sin(t * (1.5 + 1.1 * windAmt) + phase) * 0.62 +
                 sin(t * (3.1 + 1.9 * windAmt) + phase * 1.7) * 0.38;
    vec3 windDir = safeDir(vec3(cos(frame.weather.w), 0.0, sin(frame.weather.w)), vec3(1.0, 0.0, 0.0));

    // Steady drag bends the hair downwind, the wave is the oscillation about it.
    vec3 drag = windDir * (0.010 + 0.085 * windAmt) * mass;
    vec3 gust = windDir * (0.045 * windAmt) * sway * mass;
    // Sideways flutter perpendicular to the wind.
    vec3 side = vec3(-windDir.z, 0.0, windDir.x);
    vec3 flutter = side * (0.030 * windAmt + 0.010) * sway * mass;

    // Gravity: free length sags, and wet hair is much heavier.
    float sag = (0.055 * gravAmt + 0.075 * wetAmt) * pow(clamp(w, 0.0, 1.0), 2.0);

    p += drag + gust + flutter;
    p.y -= sag;
    p2 += drag + gust + flutter;
    p2.y -= sag;
  }

  p = collideWithHead(p);
  p2 = collideWithHead(p2);

  return w;
}

void main() {
  bool shadowPass = pc.st.w > 0.5;
  mat4 mvp = shadowPass ? frame.lightMVP : frame.mvp;
  mat4 mv = shadowPass ? frame.lightView : frame.mv;

  vec3 p, p2;
  float w = evalHairVert(p, p2, inPos, inPos2, inTexW.xy);

  vec3 t = (p2 - p) * inPos2.w;
  vec4 glpos = mvp * vec4(p, 1.0);

  // Where the strand is cut. `cut_length` scales the whole head's length, the
  // per-strand jitter keeps the hairline ragged instead of a blunt hemisphere.
  float wMax = clamp(frame.hair6.x, 0.02, 1.0) *
               (1.0 - frame.hair0.y * Rand(gl_InstanceIndex, 5));

  // A degenerate tangent used to normalise to NaN and take the whole ribbon
  // with it. Fall back to a stable perpendicular to the view ray.
  vec3 tangent_view = (mv * vec4(t, 0.0)).xyz;
  vec2 b = vec2(-tangent_view.y, tangent_view.x);
  if (dot(b, b) < 1e-14) {
    vec3 fallback = safeDir(cross(normalize(-p + vec3(1e-4)), vec3(0.0, 1.0, 0.0)),
                            vec3(1.0, 0.0, 0.0));
    vec3 fbView = (mv * vec4(fallback, 0.0)).xyz;
    b = vec2(-fbView.y, fbView.x);
    tangent_view = fbView;
  }
  b = safeDir(vec3(b, 0.0), vec3(1.0, 0.0, 0.0)).xy;

  float along = clamp(w / max(wMax, 1e-3), 0.0, 1.0);

  if (shadowPass) {
    // The depth map needs *area*: a ribbon that is not expanded is a zero-area
    // quad and writes nothing, which left the hair shadow map empty (so the
    // mass never self-shadowed and read completely flat). Widen the ribbon in
    // light space instead - the depth map only needs to know how much hair the
    // light passes through, not what each strand looks like.
    const float kShadowWidth = 2.0;
    gl_Position = glpos;
    gl_Position.xy += b * inTexW.z * kShadowWidth * glpos.w;
    vViewPos = vec4(0.0);
    vTangentView = tangent_view;
    vLightPos = vec3(0.0);
    vWorld = p;
    vW = w;
    vWMax = wMax;
    vInstance = gl_InstanceIndex;
    vAlpha = 1.0;
    return;
  }

  // Expand into a screen-space ribbon. The half-width tapers towards the tip so
  // strands end in a point instead of a blunt cut, and each strand gets a small
  // random width so the mass does not read as one solid sheet.
  float taper = mix(1.0, 0.18, smoothstep(0.55, 1.0, along));
  float widthJitter = mix(0.65, 1.25, Rand(gl_InstanceIndex, 6));
  glpos.xy += b * inTexW.z * glpos.w * taper * widthJitter;

  // Alpha-to-coverage: a strand covers fewer MSAA samples where it is thin and
  // near its cut. This is what gives the silhouette soft, hair-like edges
  // instead of a polygon boundary, and it needs no sorting.
  float strandVar = mix(0.55, 1.0, Rand(gl_InstanceIndex, 7));
  vAlpha = strandVar * mix(1.0, 0.25, smoothstep(0.80, 1.0, along)) * mix(0.55, 1.0, taper);

  gl_Position = glpos;
  vViewPos = mv * vec4(p, 1.0);
  vTangentView = tangent_view;
  vLightPos = (frame.lightView * vec4(p, 1.0)).xyz;
  vWorld = p;
  vW = w;
  vWMax = wMax;
  vInstance = gl_InstanceIndex;
}
