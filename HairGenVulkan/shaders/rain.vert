// rain.vert - volumetric rain streaks, fully procedural and instanced.
//
// No vertex buffer: 6 vertices per instance make a billboarded streak quad, and
// the instance index seeds a hash that gives each drop its own column, fall
// speed, length and phase. Drops are recycled inside a cylinder around the
// subject, so the count is constant and the density can be dialled freely.
#version 450

#include "lib/common.glsl"

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vFade;
layout(location = 2) out float vDepth;   // distance in metres, for the mist term

float hash1(uint x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return float(x & 0xffffffu) / 16777216.0;
}

void main() {
  uint id = uint(gl_InstanceIndex);
  float rain = clamp(frame.weather.x, 0.0, 1.0);
  float t = frame.weather2.w;

  // ---- per drop constants -------------------------------------------------
  float r0 = hash1(id * 3u + 1u);
  float r1 = hash1(id * 3u + 2u);
  float r2 = hash1(id * 3u + 3u);
  float r3 = hash1(id * 7u + 11u);
  float r4 = hash1(id * 13u + 5u);

  float ang = r0 * 6.28318530718;
  float rad = sqrt(r1) * 4.6;
  vec2 column = vec2(cos(ang), sin(ang)) * rad;

  // The volume follows the camera on the ground plane: the head sits near the
  // origin but an orbiting camera must never fly out of the rain.
  vec3 cam = frame.camPos.xyz;
  vec2 base = cam.xz + column;

  float fallSpeed = 7.5 + 5.5 * r2;          // m/s
  float streak = (0.14 + 0.26 * r4) * (0.40 + 0.60 * rain);

  const float yTop = 4.2;
  const float yBot = -2.2;
  float span = yTop - yBot;
  float phase = fract(r3 + t * fallSpeed / span);
  float y = cam.y + yTop - phase * span;

  // Wind: drops are dragged along the wind azimuth; the drag grows as they fall
  // (they pick up horizontal velocity), which shears the whole shower.
  vec2 windDir = vec2(cos(frame.weather.w), sin(frame.weather.w));
  vec2 drag = windDir * (0.55 + 0.9 * frame.weather.z) * (0.25 + 0.75 * phase);
  base += drag;

  vec3 vel = vec3(windDir.x * (0.8 + 2.2 * frame.weather.z), -fallSpeed,
                  windDir.y * (0.8 + 2.2 * frame.weather.z));
  vec3 along = normalize(vel);

  vec3 head = vec3(base.x, y, base.y);
  vec3 tail = head - along * streak;

  // Billboard: perpendicular to the streak and to the view ray.
  vec3 toCam = normalize(cam - head);
  vec3 side = normalize(cross(along, toCam) + vec3(1e-5, 0.0, 0.0));
  float width = 0.004 + 0.005 * r4;   // world-space half width, ~1 cm streak

  // 6 vertices -> two triangles.
  int corner = gl_VertexIndex % 6;
  float aCross = (corner == 1 || corner == 2 || corner == 4) ? 1.0 : -1.0;
  float aAlong = (corner == 2 || corner == 3 || corner == 4) ? 1.0 : 0.0;
  // (0,0) (-1,0) (1,1) (-1,0) (1,1) (1,0)

  vec3 p = mix(head, tail, aAlong) + side * aCross * width;

  // Fade at the top and bottom of the volume so drops never pop in.
  float fade = smoothstep(0.0, 0.18, phase) * smoothstep(1.0, 0.82, phase);
  // Fade very close to the lens (a drop 20 cm away would be a full-screen bar).
  float dist = length(cam - p);
  fade *= smoothstep(0.35, 1.4, dist);
  // And fade far drops into the mist.
  fade *= 1.0 - smoothstep(9.0, 16.0, dist);

  vFade = fade;
  vDepth = dist;
  vUV = vec2(aCross * 0.5 + 0.5, aAlong);

  gl_Position = frame.mvp * vec4(p, 1.0);
}
