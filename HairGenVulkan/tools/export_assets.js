#!/usr/bin/env node
/*
 * export_assets.js - converts the browser demo's JavaScript data dumps (and the
 * Marschner lookup PNGs) into the binary asset bundle used by the Vulkan
 * renderer.
 *
 *   node tools/export_assets.js [--src <demo dir>] [--out <assets dir>]
 *
 * The hair-mesh 3D texture generation below is a direct port of
 * hairmesh.js:InitRendering() from the original WebGL demo, so the exported
 * data is bit-comparable with what cemyuksel.com/research/hairmesh_rendering
 * renders in the browser.
 */
'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { execFileSync } = require('child_process');

/* ------------------------------------------------------------------ args */

const args = process.argv.slice(2);
function arg(name, def) {
  const i = args.indexOf('--' + name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
}
const ROOT = path.resolve(__dirname, '..', '..');
const SRC = path.resolve(arg('src', ROOT));
const OUT = path.resolve(arg('out', path.join(__dirname, '..', 'assets')));
fs.mkdirSync(OUT, { recursive: true });

/* ------------------------------------------------------------- png decode */

function decodePNG(buf) {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a png');
  let off = 8, w = 0, h = 0, bitDepth = 0, colorType = 0, interlace = 0;
  const idat = [];
  let palette = null, trns = null;
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.toString('ascii', off + 4, off + 8);
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === 'IHDR') {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9]; interlace = data[12];
    } else if (type === 'PLTE') palette = Buffer.from(data);
    else if (type === 'tRNS') trns = Buffer.from(data);
    else if (type === 'IDAT') idat.push(Buffer.from(data));
    else if (type === 'IEND') break;
    off += 12 + len;
  }
  if (bitDepth !== 8) throw new Error('only 8-bit PNGs supported');
  if (interlace !== 0) throw new Error('interlaced PNGs not supported');
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const chans = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colorType];
  if (chans === undefined) throw new Error('unsupported PNG color type ' + colorType);
  const bpp = chans;                     // bytes per pixel (8 bit)
  const stride = w * bpp;
  const out = Buffer.alloc(w * h * 4);
  let prev = Buffer.alloc(stride);
  let p = 0;
  for (let y = 0; y < h; ++y) {
    const filter = raw[p++];
    const line = Buffer.from(raw.subarray(p, p + stride)); p += stride;
    for (let x = 0; x < stride; ++x) {
      const a = x >= bpp ? line[x - bpp] : 0;
      const b = prev[x];
      const c = x >= bpp ? prev[x - bpp] : 0;
      let v = line[x];
      switch (filter) {
        case 0: break;
        case 1: v = (v + a) & 255; break;
        case 2: v = (v + b) & 255; break;
        case 3: v = (v + ((a + b) >> 1)) & 255; break;
        case 4: {
          const pp = a + b - c;
          const pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
          v = (v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c)) & 255;
          break;
        }
        default: throw new Error('bad filter ' + filter);
      }
      line[x] = v;
    }
    for (let x = 0; x < w; ++x) {
      const o = (y * w + x) * 4;
      if (colorType === 3) {
        const ix = line[x] * 3;
        out[o] = palette[ix]; out[o + 1] = palette[ix + 1]; out[o + 2] = palette[ix + 2];
        out[o + 3] = trns && line[x] < trns.length ? trns[line[x]] : 255;
      } else if (chans === 1 || chans === 2) {
        out[o] = out[o + 1] = out[o + 2] = line[x * chans];
        out[o + 3] = chans === 2 ? line[x * chans + 1] : 255;
      } else {
        out[o] = line[x * bpp]; out[o + 1] = line[x * bpp + 1]; out[o + 2] = line[x * bpp + 2];
        out[o + 3] = chans === 4 ? line[x * bpp + 3] : 255;
      }
    }
    prev = line;
  }
  return { width: w, height: h, data: out };
}

function loadImage(file) {
  if (/\.png$/i.test(file)) return decodePNG(fs.readFileSync(file));
  // JPEG (or anything else ImageMagick understands) -> png on stdout
  try {
    const png = execFileSync('convert', [file, 'png:-'], { maxBuffer: 1 << 28 });
    return decodePNG(png);
  } catch (e) {
    throw new Error('cannot decode ' + file + ' (install ImageMagick for JPEG input): ' + e.message);
  }
}

/* ------------------------------------------------------- demo data load */

const DEMO_FILES = ['july-low.js', 'july_hairmesh3.js', 'july_rootsamples.js', 'july_colliders.js', 'noise.js'];
for (const f of DEMO_FILES) {
  const src = fs.readFileSync(path.join(SRC, f), 'utf8');
  // The demo files are plain `name = [...]` assignments.
  (0, eval)(src.replace(/^\s*(?:const|let|var)\s+/gm, ''));
}
const modelShift = [0, -0.2, 0];

/* --------------------------------------------------- transform (centering) */

const big = 99999;
let pos_min = [big, big, big], pos_max = [-big, -big, -big];
for (let i = 0; i < obj_vert_pos.length; i += 3)
  for (let j = 0; j < 3; ++j) {
    pos_min[j] = Math.min(obj_vert_pos[i + j], pos_min[j]);
    pos_max[j] = Math.max(obj_vert_pos[i + j], pos_max[j]);
  }
for (const v of hairmesh_verts)
  for (let j = 0; j < 3; ++j) {
    pos_min[j] = Math.min(v.p[j], pos_min[j]);
    pos_max[j] = Math.max(v.p[j], pos_max[j]);
  }
const center = [(pos_max[0] + pos_min[0]) / 2, (pos_max[1] + pos_min[1]) / 2, (pos_max[2] + pos_min[2]) / 2];
const sizeX = pos_max[0] - pos_min[0], sizeY = pos_max[1] - pos_min[1], sizeZ = pos_max[2] - pos_min[2];
const scale = 2 / Math.max(sizeX, sizeY, sizeZ);

function xform(p) {
  return [
    (p[0] - center[0]) * scale + modelShift[0],
    (p[1] - center[1]) * scale + modelShift[1],
    (p[2] - center[2]) * scale + modelShift[2],
  ];
}

for (let i = 0; i < obj_vert_pos.length; i += 3) {
  const q = xform([obj_vert_pos[i], obj_vert_pos[i + 1], obj_vert_pos[i + 2]]);
  obj_vert_pos[i] = q[0]; obj_vert_pos[i + 1] = q[1]; obj_vert_pos[i + 2] = q[2];
}
for (const v of hairmesh_verts) v.p = xform(v.p);

/* ------------------------------------------ hair mesh 3D texture (ported) */

let maxLevel = 0;
const hairMeshVertLevels = new Array(hairmesh_verts.length);
for (let i = 0; i < hairmesh_bundles.length; ++i) {
  let level = -1;
  const b = hairmesh_bundles[i];
  let v = b[0];
  while (v >= 0) { v = hairmesh_verts[v].prev; level++; }
  if (maxLevel < level) maxLevel = level;
  for (let j = 0; j < b.length; ++j) {
    let vv = b[j];
    for (let l = level; l >= 0; --l) { hairMeshVertLevels[vv] = l; vv = hairmesh_verts[vv].prev; }
  }
}
const tex_hairmesh_depth = 3 * maxLevel + 1;

function GenerateBundleLayout(bundles) {
  const layout = new Array();
  for (let i = 0; i < hairmesh_bundleLayout.length; ++i) {
    const b = bundles[i];
    const l = hairmesh_bundleLayout[i];
    if (!Array.isArray(layout[l[1]])) layout[l[1]] = new Array();
    if (!Array.isArray(layout[l[1] + 1])) layout[l[1] + 1] = new Array();
    layout[l[1]][l[0]] = b[0];
    layout[l[1]][l[0] + 1] = b[1];
    layout[l[1] + 1][l[0] + 1] = b[2];
    layout[l[1] + 1][l[0]] = b[3];
  }
  for (let i = 0; i < hairmesh_bundleLayout.length; ++i) {
    const b = bundles[i], l = hairmesh_bundleLayout[i];
    if (layout[l[1]][l[0]] !== b[0] || layout[l[1]][l[0] + 1] !== b[1] ||
        layout[l[1] + 1][l[0] + 1] !== b[2] || layout[l[1] + 1][l[0]] !== b[3])
      console.warn('hair mesh layout warning at bundle ' + i);
  }
  return layout;
}
const hairMeshTextureLayout = GenerateBundleLayout(hairmesh_bundles);
const tex_hairmesh_height = hairMeshTextureLayout.length;
let tex_hairmesh_width = 0;
for (let i = 0; i < tex_hairmesh_height; ++i)
  tex_hairmesh_width = Math.max(tex_hairmesh_width, hairMeshTextureLayout[i].length);

function plen(p0, p1) {
  const a = p1[0] - p0[0], b = p1[1] - p0[1], c = p1[2] - p0[2];
  return Math.sqrt(a * a + b * b + c * c);
}

const tex_hairmesh_ids = new Array(tex_hairmesh_width * tex_hairmesh_height * tex_hairmesh_depth * 3).fill(0);
const tex_hairmesh_weights = new Array(tex_hairmesh_width * tex_hairmesh_height * tex_hairmesh_depth * 3).fill(0);
for (let y = 0; y < tex_hairmesh_height; ++y) {
  for (let x = 0; x < hairMeshTextureLayout[y].length; ++x) {
    let v1 = hairMeshTextureLayout[y][x];
    const level = hairMeshVertLevels[v1];
    let v0 = v1;
    for (let lev = level; lev > 0; --lev) {
      const p1 = hairmesh_verts[v1].p;
      const v2 = hairmesh_verts[v1].prev;
      const p2 = hairmesh_verts[v2].p;
      const l2 = plen(p1, p2);
      let s1i0 = v0, s2i0 = v0;
      let s1w0 = 1, s1w1 = 0, s1w2 = 0, s2w0 = 1, s2w1 = 0, s2w2 = 0;
      if (v0 !== v1) {
        const p0 = hairmesh_verts[v0].p;
        const l1 = plen(p0, p1);
        let L = l1 + Math.sqrt(l1 * l2);
        s1i0 = v0;
        s1w0 = -l2 / L;
        s1w1 = (l2 - l1) / L + 2;
        s1w2 = l1 / L - 1;
        if (lev > 1) {
          const v3 = hairmesh_verts[v2].prev;
          const p3 = hairmesh_verts[v3].p;
          const l3 = plen(p3, p2);
          L = l3 + Math.sqrt(l3 * l2);
          s2i0 = v3;
          s2w0 = -l2 / L;
          s2w1 = (l2 - l3) / L + 2;
          s2w2 = l3 / L - 1;
        } else {
          s2w0 = -s1w0; s2w1 = s1w1; s2w2 = s1w2;
        }
      } else {
        if (lev > 1) {
          const v3 = hairmesh_verts[v2].prev;
          const p3 = hairmesh_verts[v3].p;
          const l3 = plen(p3, p2);
          const L = l3 + Math.sqrt(l3 * l2);
          s1i0 = v3; s2i0 = v3;
          s1w0 = l2 / L; s1w1 = (l2 - l3) / L + 2; s1w2 = l3 / L - 1;
          s2w0 = -s1w0; s2w1 = s1w1; s2w2 = s1w2;
        } else {
          s1i0 = v1; s2i0 = v2;
        }
      }
      const z = 3 * lev;
      const i = (z * tex_hairmesh_height + y) * tex_hairmesh_width + x;
      const dz = tex_hairmesh_width * tex_hairmesh_height;
      tex_hairmesh_ids[3 * i] = v1; tex_hairmesh_ids[3 * i + 1] = 0; tex_hairmesh_ids[3 * i + 2] = 0;
      tex_hairmesh_ids[3 * (i - dz)] = s1i0;
      tex_hairmesh_ids[3 * (i - dz) + 1] = v1;
      tex_hairmesh_ids[3 * (i - dz) + 2] = v2;
      tex_hairmesh_ids[3 * (i - 2 * dz)] = s2i0;
      tex_hairmesh_ids[3 * (i - 2 * dz) + 1] = v2;
      tex_hairmesh_ids[3 * (i - 2 * dz) + 2] = v1;
      tex_hairmesh_weights[3 * i] = 1; tex_hairmesh_weights[3 * i + 1] = 0; tex_hairmesh_weights[3 * i + 2] = 0;
      tex_hairmesh_weights[3 * (i - dz)] = s1w0;
      tex_hairmesh_weights[3 * (i - dz) + 1] = s1w1;
      tex_hairmesh_weights[3 * (i - dz) + 2] = s1w2;
      tex_hairmesh_weights[3 * (i - 2 * dz)] = s2w0;
      tex_hairmesh_weights[3 * (i - 2 * dz) + 1] = s2w1;
      tex_hairmesh_weights[3 * (i - 2 * dz) + 2] = s2w2;
      v0 = v1;
      v1 = v2;
    }
    const i = y * tex_hairmesh_width + x;
    tex_hairmesh_ids[3 * i] = v1;
    tex_hairmesh_ids[3 * i + 1] = 0;
    tex_hairmesh_ids[3 * i + 2] = 0;
    tex_hairmesh_weights[3 * i] = 1;
    tex_hairmesh_weights[3 * i + 1] = 0;
    tex_hairmesh_weights[3 * i + 2] = 0;
  }
}

const tex_hairmesh_data = new Array(tex_hairmesh_ids.length * 3).fill(0);
for (let i = 0; i < tex_hairmesh_ids.length; i += 3) {
  const v0 = tex_hairmesh_ids[i], v1 = tex_hairmesh_ids[i + 1], v2 = tex_hairmesh_ids[i + 2];
  const w0 = tex_hairmesh_weights[i], w1 = tex_hairmesh_weights[i + 1], w2 = tex_hairmesh_weights[i + 2];
  if (w2 === 0) {
    const p = hairmesh_verts[w0 === 1 ? v0 : v0].p;
    tex_hairmesh_data[i] = hairmesh_verts[v0].p[0];
    tex_hairmesh_data[i + 1] = hairmesh_verts[v0].p[1];
    tex_hairmesh_data[i + 2] = hairmesh_verts[v0].p[2];
  } else {
    if (w0 < 0) {
      tex_hairmesh_data[i] = w0 * hairmesh_verts[v0].p[0];
      tex_hairmesh_data[i + 1] = w0 * hairmesh_verts[v0].p[1];
      tex_hairmesh_data[i + 2] = w0 * hairmesh_verts[v0].p[2];
    } else {
      const dx = hairmesh_verts[v2].p[0] - hairmesh_verts[v1].p[0];
      const dy = hairmesh_verts[v2].p[1] - hairmesh_verts[v1].p[1];
      const dz = hairmesh_verts[v2].p[2] - hairmesh_verts[v1].p[2];
      const rx = hairmesh_verts[v0].p[0] - hairmesh_verts[v2].p[0];
      const ry = hairmesh_verts[v0].p[1] - hairmesh_verts[v2].p[1];
      const rz = hairmesh_verts[v0].p[2] - hairmesh_verts[v2].p[2];
      const s = 1 + 2 * (dx * rx + dy * ry + dz * rz) / (dx * dx + dy * dy + dz * dz);
      tex_hairmesh_data[i] = -w0 * (hairmesh_verts[v0].p[0] - s * dx);
      tex_hairmesh_data[i + 1] = -w0 * (hairmesh_verts[v0].p[1] - s * dy);
      tex_hairmesh_data[i + 2] = -w0 * (hairmesh_verts[v0].p[2] - s * dz);
    }
    tex_hairmesh_data[i] += w1 * hairmesh_verts[v1].p[0] + w2 * hairmesh_verts[v2].p[0];
    tex_hairmesh_data[i + 1] += w1 * hairmesh_verts[v1].p[1] + w2 * hairmesh_verts[v2].p[1];
    tex_hairmesh_data[i + 2] += w1 * hairmesh_verts[v1].p[2] + w2 * hairmesh_verts[v2].p[2];
  }
}

/* tex_uv (bundle texcoords per grid point) */
const uvTextureLayout = GenerateBundleLayout(hairmesh_bundleTexCoords);
const tex_uv_data = new Array(tex_hairmesh_width * tex_hairmesh_height * 2).fill(0);
for (let y = 0; y < tex_hairmesh_height; ++y)
  for (let x = 0; x < uvTextureLayout[y].length; ++x) {
    const tc = hairmesh_texCoords[uvTextureLayout[y][x]];
    tex_uv_data[2 * (y * tex_hairmesh_width + x)] = tc[0];
    tex_uv_data[2 * (y * tex_hairmesh_width + x) + 1] = tc[1];
  }

/* u/v tangent directions per hair-mesh level */
const tex_vertTri = new Array(hairmesh_verts.length);
function buildTMatrix(tc0, tc1, tc2) {
  const m0 = tc1[0] - tc0[0], m1 = tc1[1] - tc0[1];
  const m2 = tc2[0] - tc0[0], m3 = tc2[1] - tc0[1];
  return [m3, -m1, -m2, m0];
}
function setNewTri(v, vn, vp, m) {
  if (!Array.isArray(tex_vertTri[v])) tex_vertTri[v] = new Array();
  tex_vertTri[v].push({ v1: vn, v2: vp, m: m });
}
for (let i = 0; i < hairmesh_bundles.length; ++i) {
  const b = hairmesh_bundles[i], bt = hairmesh_bundleTexCoords[i];
  const tc0 = hairmesh_texCoords[bt[0]], tc1 = hairmesh_texCoords[bt[1]], tc2 = hairmesh_texCoords[bt[2]];
  if (b.length === 4) {
    const tc3 = hairmesh_texCoords[bt[3]];
    const m0 = buildTMatrix(tc0, tc1, tc3), m1 = buildTMatrix(tc1, tc2, tc0);
    const m2 = buildTMatrix(tc2, tc3, tc1), m3 = buildTMatrix(tc3, tc0, tc2);
    const v = [b[0], b[1], b[2], b[3]];
    while (v[0] >= 0) {
      setNewTri(v[0], v[1], v[3], m0);
      setNewTri(v[1], v[2], v[0], m1);
      setNewTri(v[2], v[3], v[1], m2);
      setNewTri(v[3], v[0], v[2], m3);
      for (let k = 0; k < 4; ++k) v[k] = hairmesh_verts[v[k]].prev;
    }
  } else {
    const m0 = buildTMatrix(tc0, tc1, tc2), m1 = buildTMatrix(tc1, tc2, tc0), m2 = buildTMatrix(tc2, tc0, tc1);
    const v = [b[0], b[1], b[2]];
    while (v[0] >= 0) {
      setNewTri(v[0], v[1], v[2], m0);
      setNewTri(v[1], v[2], v[0], m1);
      setNewTri(v[2], v[0], v[1], m2);
      for (let k = 0; k < 3; ++k) v[k] = hairmesh_verts[v[k]].prev;
    }
  }
}
const tex_uvw_ids = new Array(tex_hairmesh_width * tex_hairmesh_height * (maxLevel + 1)).fill(-1);
for (let y = 0; y < tex_hairmesh_height; ++y)
  for (let x = 0; x < hairMeshTextureLayout[y].length; ++x) {
    let v = hairMeshTextureLayout[y][x];
    const level = hairMeshVertLevels[v];
    for (let lev = level; lev >= 0; --lev) {
      tex_uvw_ids[(lev * tex_hairmesh_height + y) * tex_hairmesh_width + x] = v;
      v = hairmesh_verts[v].prev;
    }
  }
const tex_uw_data = new Array(tex_hairmesh_width * tex_hairmesh_height * (maxLevel + 1) * 4).fill(0);
const tex_v_data = new Array(tex_hairmesh_width * tex_hairmesh_height * (maxLevel + 1) * 3).fill(0);
function normalizeVec(v) { const l = Math.hypot(v[0], v[1], v[2]) || 1; v[0] /= l; v[1] /= l; v[2] /= l; }
for (let i = 0; i < tex_uvw_ids.length; ++i) {
  if (tex_uvw_ids[i] < 0) continue;
  const vid = tex_uvw_ids[i];
  const vertTri = tex_vertTri[vid] || [];
  const u = [0, 0, 0], v = [0, 0, 0];
  const p0 = hairmesh_verts[vid].p;
  for (const t of vertTri) {
    const p1 = hairmesh_verts[t.v1].p, p2 = hairmesh_verts[t.v2].p;
    for (let k = 0; k < 3; ++k) {
      const p10 = p1[k] - p0[k], p20 = p2[k] - p0[k];
      u[k] += p10 * t.m[0] + p20 * t.m[1];
      v[k] += p10 * t.m[2] + p20 * t.m[3];
    }
  }
  normalizeVec(u); normalizeVec(v);
  tex_uw_data[4 * i] = u[0]; tex_uw_data[4 * i + 1] = u[1]; tex_uw_data[4 * i + 2] = u[2];
  tex_uw_data[4 * i + 3] = hairmesh_verts[vid].w;
  tex_v_data[3 * i] = v[0]; tex_v_data[3 * i + 1] = v[1]; tex_v_data[3 * i + 2] = v[2];
}

/* --------------------------------------------- clump texture (CPU raster) */

const CLUMP_RES = 256, CLUMP_NOISE = 1.0, CLUMP_FREQ = 1.0;

// bilinear sample of tex_uv
function sampleUV(u, v) {
  const X = u * tex_hairmesh_width - 0.5, Y = v * tex_hairmesh_height - 0.5;
  const x0 = Math.floor(X), y0 = Math.floor(Y);
  const fx = X - x0, fy = Y - y0;
  const cx = (x) => Math.min(tex_hairmesh_width - 1, Math.max(0, x));
  const cy = (y) => Math.min(tex_hairmesh_height - 1, Math.max(0, y));
  function get(x, y) {
    const i = 2 * (cy(y) * tex_hairmesh_width + cx(x));
    return [tex_uv_data[i], tex_uv_data[i + 1]];
  }
  const a = get(x0, y0), b = get(x0 + 1, y0), c = get(x0, y0 + 1), d = get(x0 + 1, y0 + 1);
  return [
    (a[0] * (1 - fx) + b[0] * fx) * (1 - fy) + (c[0] * (1 - fx) + d[0] * fx) * fy,
    (a[1] * (1 - fx) + b[1] * fx) * (1 - fy) + (c[1] * (1 - fx) + d[1] * fx) * fy,
  ];
}
// trilinear sample of the noise volume (repeat wrap)
function sampleNoise(x, y, z, ch) {
  const N = noise_size;
  const X = ((x % 1) + 1) % 1 * N - 0.5, Y = ((y % 1) + 1) % 1 * N - 0.5, Z = ((z % 1) + 1) % 1 * N - 0.5;
  const x0 = Math.floor(X), y0 = Math.floor(Y), z0 = Math.floor(Z);
  const fx = X - x0, fy = Y - y0, fz = Z - z0;
  const w = (i) => ((i % N) + N) % N;
  const get = (x, y, z) => noise_data[3 * ((w(z) * N + w(y)) * N + w(x)) + ch] / 255;
  const c000 = get(x0, y0, z0), c100 = get(x0 + 1, y0, z0), c010 = get(x0, y0 + 1, z0), c110 = get(x0 + 1, y0 + 1, z0);
  const c001 = get(x0, y0, z0 + 1), c101 = get(x0 + 1, y0, z0 + 1), c011 = get(x0, y0 + 1, z0 + 1), c111 = get(x0 + 1, y0 + 1, z0 + 1);
  const l = (a, b, t) => a + (b - a) * t;
  return l(l(l(c000, c100, fx), l(c010, c110, fx), fy), l(l(c001, c101, fx), l(c011, c111, fx), fy), fz);
}

// quad (in uv space) covered by each bundle: its 4 corner uv values
const bundleQuads = hairmesh_bundleLayout.map((l) => {
  const cx = [l[0] + 0.5, l[0] + 1.5, l[0] + 1.5, l[0] + 0.5];
  const cy = [l[1] + 0.5, l[1] + 0.5, l[1] + 1.5, l[1] + 1.5];
  const uvs = cx.map((x, i) => sampleUV((x) / tex_hairmesh_width, cy[i] / tex_hairmesh_height));
  return { st: [l[0] + 0.5, l[1] + 0.5], uv: uvs };
});

function invBilinear(uv, quad) {
  // solve uv = bilinear(s,t) on the quad (2 triangles, like the GPU raster)
  const [p0, p1, p2, p3] = quad.uv;
  let best = null;
  const tris = [[p0, p1, p2], [p0, p2, p3]];
  for (const [a, b, c] of tris) {
    const d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
    if (Math.abs(d) < 1e-12) continue;
    const w1 = ((uv[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (uv[1] - a[1])) / d;
    const w2 = ((b[0] - a[0]) * (uv[1] - a[1]) - (uv[0] - a[0]) * (b[1] - a[1])) / d;
    const w0 = 1 - w1 - w2;
    if (w1 >= -1e-4 && w2 >= -1e-4 && w0 >= -1e-4) {
      // barycentric -> (s,t) in the 1x1 cell
      const s = w0 * 0 + w1 * 1 + w2 * 1;
      const t = w0 * 0 + w1 * 0 + w2 * 1;
      const score = Math.min(w0, w1, w2);
      if (!best || score > best.score) best = { s, t, score };
    }
  }
  return best;
}

function renderClumpTexture() {
  const R = CLUMP_RES;
  const tex = new Uint8Array(R * R * 4);
  const noiseScale = CLUMP_NOISE / R;
  const bilinearUV = (bq, s, t) => [
    (bq.uv[0][0] * (1 - s) + bq.uv[1][0] * s) * (1 - t) + (bq.uv[2][0] * (1 - s) + bq.uv[3][0] * s) * t,
    (bq.uv[0][1] * (1 - s) + bq.uv[1][1] * s) * (1 - t) + (bq.uv[2][1] * (1 - s) + bq.uv[3][1] * s) * t,
  ];
  for (let y = 0; y < R; ++y) {
    for (let x = 0; x < R; ++x) {
      const q = [(x + 0.5) / R, (y + 0.5) / R];
      // bundles are drawn back to front; the last one covering a texel wins
      for (const bq of bundleQuads) {
        let s = 0, t = 0, hit = null;
        let guess = q;
        for (let it = 0; it < 4; ++it) {
          const d0 = sampleNoise(guess[0] * CLUMP_FREQ, guess[1] * CLUMP_FREQ, 0, 0) * 2 - 1;
          const d1 = sampleNoise(guess[0] * CLUMP_FREQ, guess[1] * CLUMP_FREQ, 0, 1) * 2 - 1;
          hit = invBilinear([q[0] - d0 * noiseScale, q[1] - d1 * noiseScale], bq);
          if (!hit) break;
          s = hit.s; t = hit.t;
          guess = bilinearUV(bq, s, t);
        }
        if (!hit) continue;
        const stp = [bq.st[0] + s, bq.st[1] + t];
        const stx = stp[0] / tex_hairmesh_width, sty = stp[1] / tex_hairmesh_height;
        const s0x = Math.floor(stx * 256) / 256, s0y = Math.floor(sty * 256) / 256;
        const s1x = stx * 256 - Math.floor(stx * 256), s1y = sty * 256 - Math.floor(sty * 256);
        const o = 4 * (y * R + x);
        tex[o] = Math.round(s0x * 255);
        tex[o + 1] = Math.round(s0y * 255);
        tex[o + 2] = Math.round(s1x * 255);
        tex[o + 3] = Math.round(s1y * 255);
      }
    }
  }
  // extend pass: fill empty texels from the 8 neighbours (as the demo's clumpExt shader does)
  const ext = Uint8Array.from(tex);
  for (let y = 0; y < R; ++y)
    for (let x = 0; x < R; ++x) {
      const o = 4 * (y * R + x);
      if (tex[o] !== 0 || tex[o + 1] !== 0 || tex[o + 2] !== 0 || tex[o + 3] !== 0) continue;
      const nbr = [[-1, 0], [1, 0], [0, -1], [0, 1], [-1, -1], [1, -1], [-1, 1], [1, 1]];
      for (const [dx, dy] of nbr) {
        const nx = Math.min(R - 1, Math.max(0, x + dx)), ny = Math.min(R - 1, Math.max(0, y + dy));
        const p = 4 * (ny * R + nx);
        if (tex[p] || tex[p + 1] || tex[p + 2] || tex[p + 3]) {
          ext[o] = tex[p]; ext[o + 1] = tex[p + 1]; ext[o + 2] = tex[p + 2]; ext[o + 3] = tex[p + 3];
          break;
        }
      }
    }
  let empty = 0;
  for (let i = 0; i < R * R; ++i) if (!ext[4 * i] && !ext[4 * i + 1] && !ext[4 * i + 2] && !ext[4 * i + 3]) empty++;
  if (process.env.HGEN_DEBUG_CLUMP) {
    let mn = [9, 9], mx = [-9, -9];
    for (const bq of bundleQuads) for (const p of bq.uv) {
      mn = [Math.min(mn[0], p[0]), Math.min(mn[1], p[1])];
      mx = [Math.max(mx[0], p[0]), Math.max(mx[1], p[1])];
    }
    console.log('  clump quad uv range', mn, mx);
    let filled = 0;
    for (let i = 0; i < R * R; ++i) if (tex[4 * i] || tex[4 * i + 1] || tex[4 * i + 2] || tex[4 * i + 3]) filled++;
    console.log('  clump filled by quads:', filled, '/', R * R);
  }
  console.log(`  clump texture ${R}x${R}: ${empty} unresolved texels`);
  return ext;
}

/* ------------------------------------------------------------ head mesh */

const headPos = new Float32Array(obj_vert_pos);
const headNrm = new Float32Array(obj_normals);
const headUV = new Float32Array(obj_texCoords);
// triangle strip -> triangle list
const strip = obj_indices;
const headIdx = [];
for (let i = 0; i + 2 < strip.length; ++i) {
  const a = strip[i], b = strip[i + 1], c = strip[i + 2];
  if (a === b || b === c || a === c) continue;
  if (i & 1) headIdx.push(b, a, c); else headIdx.push(a, b, c);
}

/* --------------------------------------------------------------- bundle */

const chunks = [];
function addChunk(name, dx, dy, dz, channels, format, data) {
  chunks.push({ name, dx, dy, dz, channels, format, data: Buffer.from(data) });
}
const f32 = (arr) => { const b = Buffer.alloc(arr.length * 4); for (let i = 0; i < arr.length; ++i) b.writeFloatLE(arr[i], i * 4); return b; };
const u8 = (arr) => Buffer.from(arr);
const u16 = (arr) => { const b = Buffer.alloc(arr.length * 2); for (let i = 0; i < arr.length; ++i) b.writeUInt16LE(arr[i], i * 2); return b; };
const u32 = (arr) => { const b = Buffer.alloc(arr.length * 4); for (let i = 0; i < arr.length; ++i) b.writeUInt32LE(arr[i], i * 4); return b; };

console.log('hair mesh texture: %dx%dx%d, %d levels', tex_hairmesh_width, tex_hairmesh_height, tex_hairmesh_depth, maxLevel + 1);

addChunk('head.pos', headPos.length / 3, 1, 1, 3, 2, f32(headPos));
addChunk('head.nrm', headNrm.length / 3, 1, 1, 3, 2, f32(headNrm));
addChunk('head.uv', headUV.length / 2, 1, 1, 2, 2, f32(headUV));
addChunk('head.idx', headIdx.length, 1, 1, 1, 1, u32(headIdx));

addChunk('hairmesh.pos', tex_hairmesh_width, tex_hairmesh_height, tex_hairmesh_depth, 3, 2, f32(tex_hairmesh_data));
addChunk('hairmesh.uv', tex_hairmesh_width, tex_hairmesh_height, 1, 2, 2, f32(tex_uv_data));
addChunk('hairmesh.uw', tex_hairmesh_width, tex_hairmesh_height, maxLevel + 1, 4, 2, f32(tex_uw_data));
addChunk('hairmesh.v', tex_hairmesh_width, tex_hairmesh_height, maxLevel + 1, 3, 2, f32(tex_v_data));

// The bundle -> hair-mesh texel table the runtime uses for its per-bundle draws.
const bundleST = [];
for (let i = 0; i < hairmesh_bundleLayout.length; ++i)
  bundleST.push(hairmesh_bundleLayout[i][0] + 0.5, hairmesh_bundleLayout[i][1] + 0.5);

const clump = renderClumpTexture();
addChunk('hair.clump', CLUMP_RES, CLUMP_RES, 1, 4, 0, u8(clump));
addChunk('hair.bundles', bundleST.length / 2, 1, 1, 2, 2, f32(bundleST));

/* --------------------------------------- root samples (hair strip roots) */
// july_rootsamples.js: an explicit distribution of strip roots over the
// bundles (bundID, s, t, rot). Group by bundle and emit an exclusive prefix
// table so the renderer can draw one instance range per bundle.
const rootBucket = Array.from({ length: hairmesh_bundleLayout.length }, () => []);
for (const r of root_samples) rootBucket[r.bundID].push([r.s, r.t]);
const rootST = [];
const rootBase = [];
const rootCount = [];
for (let b = 0; b < rootBucket.length; ++b) {
  rootBase.push(rootST.length / 2);
  rootCount.push(rootBucket[b].length);
  for (const [s, t] of rootBucket[b]) rootST.push(s, t);
}
console.log(`  root samples: ${root_samples.length} roots over ${hairmesh_bundleLayout.length} bundles`);
addChunk('roots.st', rootST.length / 2, 1, 1, 2, 2, f32(rootST));
addChunk('roots.base', rootBase.length, 1, 1, 1, 3, u32(rootBase));
addChunk('roots.count', rootCount.length, 1, 1, 1, 3, u32(rootCount));

/* ------------------------------------------------- colliders (hair vs head) */
// july_colliders.js is in raw model space; apply the exact same centering
// transform the demo applies before uploading to the simulator. Each entry is
// either one sphere (xyz, r) or a pair of spheres (xyz, r, xyz, r).
const COLLIDER_STRIDE = 8;
const colliderData = new Float32Array(obj_colliders.length * COLLIDER_STRIDE);
let colliderCount = 0;
for (const c of obj_colliders) {
  const single = c.length <= 4;
  const p0 = xform([c[0], c[1], c[2]]);
  const r0 = c[3] * scale;
  const p1 = single ? p0 : xform([c[4], c[5], c[6]]);
  const r1 = single ? 0 : c[7] * scale;
  const o = colliderCount * COLLIDER_STRIDE;
  colliderData.set([p0[0], p0[1], p0[2], r0, p1[0], p1[1], p1[2], r1], o);
  colliderCount++;
}
console.log(`  colliders: ${colliderCount} capsules/spheres`);
addChunk('colliders', colliderCount, 1, 1, COLLIDER_STRIDE, 2, f32(colliderData));

/* -------------------------------------------------- validate the clump map */

{
  const R = CLUMP_RES;
  let worst = 0, n = 0;
  for (let b = 0; b < hairmesh_bundleLayout.length; ++b) {
    const l = hairmesh_bundleLayout[b];
    const stp = [(l[0] + 0.5) / tex_hairmesh_width, (l[1] + 0.5) / tex_hairmesh_height];
    const uv = sampleUV(stp[0], stp[1]);
    const x = Math.min(R - 1, Math.max(0, Math.floor(uv[0] * R)));
    const y = Math.min(R - 1, Math.max(0, Math.floor(uv[1] * R)));
    const o = 4 * (y * R + x);
    const stc = [
      (clump[o] + clump[o + 2] / 256) / 255 * tex_hairmesh_width,
      (clump[o + 1] + clump[o + 3] / 256) / 255 * tex_hairmesh_height,
    ];
    const err = Math.hypot(stc[0] - (l[0] + 0.5), stc[1] - (l[1] + 0.5));
    worst = Math.max(worst, err); n++;
  }
  console.log(`  clump map self-check: ${n} bundles, worst root error ${worst.toFixed(2)} texels`);
}

addChunk('noise3d', noise_size, noise_size, noise_size, 3, 0, u8(Array.from(noise_data)));

// Marschner lookup tables
function lut(name, comps, dim) {
  const img = loadImage(name);
  if (dim === 3) {
    const d = img.height / img.width; // 3D texture stored stacked vertically
    const out = new Uint8Array(img.width * img.width * d * comps);
    for (let z = 0; z < d; ++z)
      for (let y = 0; y < img.width; ++y)
        for (let x = 0; x < img.width; ++x) {
          const s = ((z * img.width + y) * img.width + x) * 4;
          const o = ((z * img.width + y) * img.width + x) * comps;
          for (let c = 0; c < comps; ++c) out[o + c] = img.data[s + c];
        }
    return { w: img.width, h: img.width, d, data: out };
  }
  const out = new Uint8Array(img.width * img.height * comps);
  for (let i = 0; i < img.width * img.height; ++i)
    for (let c = 0; c < comps; ++c) out[i * comps + c] = img.data[i * 4 + c];
  return { w: img.width, h: img.height, d: 1, data: out };
}
const mtlName = 'std_blond';
// The demo checkout keeps the lookup PNGs at the repo root (materials/<name>/ in
// the deployed site); accept both layouts.
const lutPath = (f) => {
  const candidates = [
    path.join(SRC, 'materials', mtlName, f),
    path.join(SRC, f),
  ];
  for (const c of candidates) if (fs.existsSync(c)) return c;
  throw new Error('lookup table not found: ' + f);
};

const nttr = lut(lutPath('N_TT_R.png'), 4, 2);
addChunk('N_TT_R', nttr.w, nttr.h, 1, 4, 0, u8(nttr.data));
const ntrt = lut(lutPath('N_TRT.png'), 3, 2);
addChunk('N_TRT', ntrt.w, ntrt.h, 1, 3, 0, u8(ntrt.data));
const nttrgi = lut(lutPath('N_TT_R_GI.png'), 4, 2);
addChunk('N_TT_R_GI', nttrgi.w, nttrgi.h, 1, 4, 0, u8(nttrgi.data));
const ntrtgi = lut(lutPath('N_TRT_GI.png'), 3, 2);
addChunk('N_TRT_GI', ntrtgi.w, ntrtgi.h, 1, 3, 0, u8(ntrtgi.data));
const gi = lut(lutPath('GI.png'), 3, 3);
addChunk('GI', gi.w, gi.h, gi.d, 3, 0, u8(gi.data));
const mgi = lut(lutPath('M_GI.png'), 3, 2);
addChunk('M_GI', mgi.w, mgi.h, 1, 3, 0, u8(mgi.data));

// skin albedo (july.jpg) - downsampled to 1024^2 RGB
const skin = loadImage(path.join(SRC, 'july.jpg'));
const skinRGB = new Uint8Array(skin.width * skin.height * 3);
for (let i = 0; i < skin.width * skin.height; ++i) {
  skinRGB[3 * i] = skin.data[4 * i];
  skinRGB[3 * i + 1] = skin.data[4 * i + 1];
  skinRGB[3 * i + 2] = skin.data[4 * i + 2];
}
addChunk('skin.albedo', skin.width, skin.height, 1, 3, 0, u8(skinRGB));

/* --------------------------------------------------------- write bundle */

const headerSize = 16 + 64 * chunks.length;
let offset = headerSize;
for (const c of chunks) { c.offset = offset; offset += c.data.length; if (offset % 256) offset += 256 - (offset % 256); }

const total = offset;
const buf = Buffer.alloc(total);
buf.write('HGENAST2', 0, 'ascii');
buf.writeUInt32LE(chunks.length, 8);
buf.writeUInt32LE(maxLevel, 12);
let hoff = 16;
for (const c of chunks) {
  buf.writeUInt32LE(c.dx, hoff);
  buf.writeUInt32LE(c.dy, hoff + 4);
  buf.writeUInt32LE(c.dz, hoff + 8);
  buf.writeUInt32LE(c.channels, hoff + 12);
  buf.writeUInt32LE(c.format, hoff + 16);
  buf.write(c.name, hoff + 20, 'ascii');
  buf.writeBigUInt64LE(BigInt(c.offset), hoff + 48);
  buf.writeBigUInt64LE(BigInt(c.data.length), hoff + 56);
  hoff += 64;
}
for (const c of chunks) c.data.copy(buf, c.offset);
fs.writeFileSync(path.join(OUT, 'hairgen_assets.bin'), buf);

console.log(`wrote ${path.join(OUT, 'hairgen_assets.bin')} (${(total / 1048576).toFixed(2)} MB), ${chunks.length} chunks`);
for (const c of chunks)
  console.log('   ' + c.name.padEnd(14) + ` ${String(c.dx).padStart(4)}x${String(c.dy).padStart(4)}x` +
    String(c.dz).padEnd(4) + ` ch${c.channels}  ${String(c.data.length).padStart(8)} B`);
