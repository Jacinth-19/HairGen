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
const origNrm = new Float32Array(obj_normals);
const headUV = new Float32Array(obj_texCoords);
// triangle strip -> triangle list
const strip = obj_indices;
const headIdx = [];
for (let i = 0; i + 2 < strip.length; ++i) {
  const a = strip[i], b = strip[i + 1], c = strip[i + 2];
  if (a === b || b === c || a === c) continue;
  if (i & 1) headIdx.push(b, a, c); else headIdx.push(a, b, c);
}
// Neck cut: the source mesh is a bust whose shoulders degenerate into large
// torn polygons (the demo never showed them). Keep everything above the neck
// line plus the neck cylinder below it, and drop the shoulder mass.
// Thresholds are in the centered model space (the vertices are transformed
// already) and can be tweaked with HGEN_NECK_CUT / HGEN_NECK_RADIUS.
{
  // Below the jaw line only the neck cylinder survives; the shoulder mass
  // reaches r ~ 0.85 at y ~ -0.5 and is what tore the silhouette.
  const cut = process.env.HGEN_NECK_CUT !== undefined ? parseFloat(process.env.HGEN_NECK_CUT) : -0.30;
  const radius = process.env.HGEN_NECK_RADIUS !== undefined ? parseFloat(process.env.HGEN_NECK_RADIUS) : 0.45;
  // The scan includes a *hair shell* over the scalp: a large sheet that rises
  // above the skull and folds forwards over the forehead. The demo is always
  // seen with strands on top of it, so it never showed; here it pokes through
  // the render as a pale flap. Drop geometry above the skull line -- the hair
  // covers that region completely.
  const scalpCut = process.env.HGEN_SCALP_CUT !== undefined ? parseFloat(process.env.HGEN_SCALP_CUT) : 10.0;
  // ... and only towards the face: the back of the head is needed for the
  // silhouette, and the hair shell there is inside the hair mass anyway.
  const keep = (i) => {
    const y = headPos[3 * i + 1];
    if (y < cut) return false;
    return y < scalpCut;
  };
  const kept = [];
  let dropped = 0;
  for (let i = 0; i < headIdx.length; i += 3) {
    if (!keep(headIdx[i]) || !keep(headIdx[i + 1]) || !keep(headIdx[i + 2])) { dropped++; continue; }
    kept.push(headIdx[i], headIdx[i + 1], headIdx[i + 2]);
  }
  headIdx.length = 0;
  for (const v of kept) headIdx.push(v);
  // --- relax the sheet ----------------------------------------------------
  // The bust is a triangle strip harvested from a scan; it has creases and
  // folds where neighbouring rows overlap, which read as hard dark wedges on
  // the forehead and cheeks no matter how the normals are averaged. A few
  // Laplacian passes over the welded vertex graph (positions, not normals)
  // relax those folds while keeping the face's shape.
  {
    const smoothIters = process.env.HGEN_SMOOTH !== undefined ? parseInt(process.env.HGEN_SMOOTH, 10) : 3;
    if (smoothIters > 0) {
      const nv = headPos.length / 3;
      const q = 1e4;
      const weld = new Map();
      const group = new Int32Array(nv);
      for (let i = 0; i < nv; ++i) {
        const k = `${Math.round(headPos[3 * i] * q)}_${Math.round(headPos[3 * i + 1] * q)}_${Math.round(headPos[3 * i + 2] * q)}`;
        let g = weld.get(k);
        if (g === undefined) { g = weld.size; weld.set(k, g); }
        group[i] = g;
      }
      const acc = new Float64Array(weld.size * 3);
      const cnt = new Int32Array(weld.size);
      for (let pass = 0; pass < smoothIters; ++pass) {
        acc.fill(0); cnt.fill(0);
        for (let i = 0; i < nv; ++i) {
          const g = group[i];
          acc[3 * g] += headPos[3 * i];
          acc[3 * g + 1] += headPos[3 * i + 1];
          acc[3 * g + 2] += headPos[3 * i + 2];
          cnt[g]++;
        }
        for (let i = 0; i < nv; ++i) {
          const g = group[i];
          const n = cnt[g];
          if (n < 2) continue;
          const ax = acc[3 * g] / n, ay = acc[3 * g + 1] / n, az = acc[3 * g + 2] / n;
          // Weighted towards the group's own centroid: enough to relax folds,
          // not enough to melt the nose.
          headPos[3 * i] += (ax - headPos[3 * i]) * 0.55;
          headPos[3 * i + 1] += (ay - headPos[3 * i + 1]) * 0.55;
          headPos[3 * i + 2] += (az - headPos[3 * i + 2]) * 0.55;
        }
      }
      console.log(`  relax: ${smoothIters} Laplacian passes over ${weld.size} welded vertices`);
    }
  }

  // --- orientation + smooth normals --------------------------------------
  // The source is a triangle *strip*, and a naive "advance by one" conversion
  // gives it a winding that is locally consistent only by accident. Two earlier
  // attempts to repair it were both wrong: per-triangle "point away from the
  // centroid" breaks on concave anatomy, and a BFS over the welded edge graph
  // (flip neighbours until they agree, then one global vote) does not survive
  // this strip's non-manifold seams -- it left ~23% of the triangles inverted,
  // which showed up as a checkerboard of back faces across the forehead and, with
  // back-face culling on, as a flat pale flap over the cheek.
  //
  // The supplied `obj_normals` turn out to be excellent: their median dot with
  // the strip's own face normal is 0.997, and 17820 of 17836 triangles already
  // agree with it. So orient every triangle *against the supplied normals* --
  // no graph, no global vote, no accumulation of errors -- and then rebuild the
  // smooth field from the corrected winding.
  {
    const nv = headPos.length / 3;
    const q = 1e4;
    const weldId = new Map();
    const groupOf = new Int32Array(nv);
    for (let i = 0; i < nv; ++i) {
      const k = `${Math.round(headPos[3 * i] * q)}_${Math.round(headPos[3 * i + 1] * q)}_${Math.round(headPos[3 * i + 2] * q)}`;
      let g = weldId.get(k);
      if (g === undefined) { g = weldId.size; weldId.set(k, g); }
      groupOf[i] = g;
    }
    // Centroid, used to repair normal groups whose faces cancel out.
    let cx3 = 0, cy3 = 0, cz3 = 0;
    for (let i = 0; i < nv; ++i) { cx3 += headPos[3 * i]; cy3 += headPos[3 * i + 1]; cz3 += headPos[3 * i + 2]; }
    cx3 /= nv; cy3 /= nv; cz3 /= nv;

    let triCount = headIdx.length / 3;
    let flipped = 0;
    for (let t = 0; t < triCount; ++t) {
      const ia = headIdx[3 * t], ib = headIdx[3 * t + 1], ic = headIdx[3 * t + 2];
      const ax = headPos[3 * ia], ay = headPos[3 * ia + 1], az = headPos[3 * ia + 2];
      const ux = headPos[3 * ib] - ax, uy = headPos[3 * ib + 1] - ay, uz = headPos[3 * ib + 2] - az;
      const vx = headPos[3 * ic] - ax, vy = headPos[3 * ic + 1] - ay, vz = headPos[3 * ic + 2] - az;
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      // Area-weighted mean of the supplied vertex normals.
      let sx = 0, sy = 0, sz = 0;
      for (const i of [ia, ib, ic]) {
        sx += origNrm[3 * i]; sy += origNrm[3 * i + 1]; sz += origNrm[3 * i + 2];
      }
      if (nx * sx + ny * sy + nz * sz < 0) {
        const tmp = headIdx[3 * t + 1];
        headIdx[3 * t + 1] = headIdx[3 * t + 2];
        headIdx[3 * t + 2] = tmp;
        ++flipped;
      }
    }
    console.log(`  orientation: ${flipped} / ${triCount} triangles flipped to match the `
                + `supplied normals (${triCount - flipped} already agreed)`);

    // ---- drop strip "stitch" triangles ---------------------------------
    // The source mesh is a triangle strip. Where the export cuts a row (the neck
    // cut) or where the strip jumps between scan rows, the strip connectivity
    // turns into long, thin triangles that span unrelated parts of the head; on
    // screen they are the flat pale sheet folded across the cheek. Compare each
    // triangle's longest edge against the median edge of the mesh and drop the
    // ones that are far above it.
    {
      const triLen = new Float64Array(triCount);
      const edges = new Float64Array(triCount);
      for (let t = 0; t < triCount; ++t) {
        const ia = headIdx[3 * t], ib = headIdx[3 * t + 1], ic = headIdx[3 * t + 2];
        const dist = (a, b) => Math.hypot(headPos[3 * a] - headPos[3 * b],
                                          headPos[3 * a + 1] - headPos[3 * b + 1],
                                          headPos[3 * a + 2] - headPos[3 * b + 2]);
        const e = Math.max(dist(ia, ib), dist(ib, ic), dist(ic, ia));
        triLen[t] = e;
        edges[t] = e;
      }
      const sorted = Array.from(edges).sort((a, b) => a - b);
      const median = sorted[Math.floor(sorted.length * 0.5)] || 0.01;
      // HGEN_STITCH_MULT tunes how aggressive the strip-repair is (relative to
      // the median edge of the mesh).
      const mult = process.env.HGEN_STITCH_MULT !== undefined
        ? parseFloat(process.env.HGEN_STITCH_MULT) : 4.5;
      const limit = Math.max(median * mult, 0.02);
      const keptT = [];
      let stitches = 0;
      for (let t = 0; t < triCount; ++t) {
        if (triLen[t] > limit) { ++stitches; continue; }
        keptT.push(headIdx[3 * t], headIdx[3 * t + 1], headIdx[3 * t + 2]);
      }
      headIdx.length = 0;
      for (const v of keptT) headIdx.push(v);
      triCount = headIdx.length / 3;
      console.log(`  stitch triangles dropped: ${stitches} / ${stitches + triCount}`
                  + ` (median edge ${median.toFixed(4)}, limit ${limit.toFixed(3)})`);
    }

    const acc = new Float64Array(weldId.size * 3);
    for (let t = 0; t < triCount; ++t) {
      const ia = headIdx[3 * t], ib = headIdx[3 * t + 1], ic = headIdx[3 * t + 2];
      const ax = headPos[3 * ia], ay = headPos[3 * ia + 1], az = headPos[3 * ia + 2];
      const ux = headPos[3 * ib] - ax, uy = headPos[3 * ib + 1] - ay, uz = headPos[3 * ib + 2] - az;
      const vx = headPos[3 * ic] - ax, vy = headPos[3 * ic + 1] - ay, vz = headPos[3 * ic + 2] - az;
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      for (const v of [ia, ib, ic]) { const g = groupOf[v]; acc[3 * g] += nx; acc[3 * g + 1] += ny; acc[3 * g + 2] += nz; }
    }
    // Laplacian smoothing of the *normal field* over the welded-group adjacency
    // graph. Averaging per-vertex face normals still leaves the shading normal
    // jumping between neighbouring groups on this scan (many groups are tiny and
    // sit on a crease), which shows up as flat faceted patches. A few passes over
    // the graph produces the smooth field a retopologised head would have.
    let degenerate = 0;
    const nrmPasses = process.env.HGEN_NRM_SMOOTH !== undefined ? parseInt(process.env.HGEN_NRM_SMOOTH, 10) : 6;
    const groupCount = weldId.size;
    const gnorm = new Float64Array(groupCount * 3);
    for (let g = 0; g < groupCount; ++g) {
      let nx = 0, ny = 0, nz = 0;
      for (let i = 0; i < nv; ++i) if (groupOf[i] === g) { nx += acc[3 * g]; break; }
      gnorm[3 * g] = acc[3 * g]; gnorm[3 * g + 1] = acc[3 * g + 1]; gnorm[3 * g + 2] = acc[3 * g + 2];
    }
    {
      const norm = (g) => {
        const len = Math.hypot(gnorm[3 * g], gnorm[3 * g + 1], gnorm[3 * g + 2]) || 1;
        gnorm[3 * g] /= len; gnorm[3 * g + 1] /= len; gnorm[3 * g + 2] /= len;
      };
      // Groups whose face normals cancel out (a pinch in the scan) would
      // normalise to zero, and normalize(0) is NaN in the shader -- which is
      // exactly the hard-edged black wedges that were left on the forehead.
      // Give them the direction from the head's centroid, which is always usable.
      for (let g = 0; g < groupCount; ++g) {
        if (Math.hypot(gnorm[3 * g], gnorm[3 * g + 1], gnorm[3 * g + 2]) < 1e-9) {
          let sx = 0, sy = 0, sz = 0, n = 0;
          for (let i = 0; i < nv; ++i)
            if (groupOf[i] === g) { sx += headPos[3 * i]; sy += headPos[3 * i + 1]; sz += headPos[3 * i + 2]; n++; }
          if (n) {
            gnorm[3 * g] = sx / n - cx3; gnorm[3 * g + 1] = sy / n - cy3; gnorm[3 * g + 2] = sz / n - cz3;
          } else {
            gnorm[3 * g + 1] = 1;
          }
          degenerate++;
        }
      }
      for (let g = 0; g < groupCount; ++g) norm(g);
      const adj = new Map();  // group -> Set of neighbouring groups
      const link = (a, b) => {
        let s = adj.get(a);
        if (!s) adj.set(a, (s = new Set()));
        s.add(b);
      };
      for (let t = 0; t < triCount; ++t) {
        const g0 = groupOf[headIdx[3 * t]], g1 = groupOf[headIdx[3 * t + 1]], g2 = groupOf[headIdx[3 * t + 2]];
        if (g0 !== g1) { link(g0, g1); link(g1, g0); }
        if (g1 !== g2) { link(g1, g2); link(g2, g1); }
        if (g2 !== g0) { link(g2, g0); link(g0, g2); }
      }
      const tmp = new Float64Array(groupCount * 3);
      for (let pass = 0; pass < nrmPasses; ++pass) {
        tmp.set(gnorm);
        for (const [g, set] of adj) {
          let nx = gnorm[3 * g] * 1.0, ny = gnorm[3 * g + 1], nz = gnorm[3 * g + 2];
          let n = 1;
          for (const o of set) { nx += tmp[3 * o]; ny += tmp[3 * o + 1]; nz += tmp[3 * o + 2]; n++; }
          gnorm[3 * g] = nx; gnorm[3 * g + 1] = ny; gnorm[3 * g + 2] = nz;
        }
        for (let g = 0; g < groupCount; ++g) norm(g);
      }
    }
    // Left/right symmetry. The scan is a mirrored strip, so geometry near the
    // centreline is doubled and its two halves carry slightly different
    // normals -- that drew a shaded seam straight down the face. Averaging each
    // vertex's normal with its mirror partner (found by a position lookup)
    // removes the seam everywhere the surface is symmetric and leaves the rest
    // alone.
    {
      const byPos = new Map();
      for (let i = 0; i < nv; ++i) {
        const k = `${groupOf[i]}`;
        if (!byPos.has(k)) byPos.set(k, i);
      }
      const mirrorOf = new Int32Array(nv).fill(-1);
      for (let i = 0; i < nv; ++i) {
        const k = `${Math.round(-headPos[3 * i] * q)}_${Math.round(headPos[3 * i + 1] * q)}_${Math.round(headPos[3 * i + 2] * q)}`;
        const j = weldId.get(k);
        if (j !== undefined) mirrorOf[i] = j;
      }
      const gmirror = new Int32Array(groupCount).fill(-1);
      for (let i = 0; i < nv; ++i)
        if (mirrorOf[i] >= 0) gmirror[groupOf[i]] = groupOf[mirrorOf[i]];
      const tmpN = Float64Array.from(gnorm);
      let paired = 0;
      for (let g = 0; g < groupCount; ++g) {
        const m = gmirror[g];
        if (m < 0 || m === g) continue;
        // Mirror x, average with the partner.
        const mx = -tmpN[3 * m], my = tmpN[3 * m + 1], mz = tmpN[3 * m + 2];
        let nx = tmpN[3 * g] * 0.5 + mx * 0.5;
        let ny = tmpN[3 * g + 1] * 0.5 + my * 0.5;
        let nz = tmpN[3 * g + 2] * 0.5 + mz * 0.5;
        const len = Math.hypot(nx, ny, nz) || 1;
        gnorm[3 * g] = nx / len; gnorm[3 * g + 1] = ny / len; gnorm[3 * g + 2] = nz / len;
        paired++;
      }
      console.log(`  normals: mirrored ${paired} groups`);
    }
    for (let i = 0; i < nv; ++i) {
      const g = groupOf[i];
      headNrm[3 * i] = gnorm[3 * g];
      headNrm[3 * i + 1] = gnorm[3 * g + 1];
      headNrm[3 * i + 2] = gnorm[3 * g + 2];
    }

    // Last-resort sign repair, kept only for genuinely broken normals.
    //
    // This used to fire on any vertex normal that pointed into a fitted head
    // ellipsoid (d < -0.15). That was a patch for the old BFS reorientation,
    // whose winding was wrong over whole regions; with the winding now taken
    // from the supplied normals, the ellipsoid test is actively harmful -- it
    // flips ~1.6 k perfectly good normals on concave anatomy (ears, jaw, the
    // sheet under the chin, the eye sockets) and turns them into back faces.
    // Only normals that are *degenerate or almost exactly inverted* are worth
    // touching now.
    {
      const inward = process.env.HGEN_NRM_REPAIR !== undefined
        ? parseFloat(process.env.HGEN_NRM_REPAIR) : -0.9;
      const c = [0, 0.02, -0.06], r = [0.46, 0.66, 0.52];
      let flippedN = 0;
      for (let i = 0; i < nv; ++i) {
        const qx = (headPos[3 * i] - c[0]) / r[0];
        const qy = (headPos[3 * i + 1] - c[1]) / r[1];
        const qz = (headPos[3 * i + 2] - c[2]) / r[2];
        let ex = qx / r[0], ey = qy / r[1], ez = qz / r[2];
        const el = Math.hypot(ex, ey, ez) || 1;
        ex /= el; ey /= el; ez /= el;
        const d = headNrm[3 * i] * ex + headNrm[3 * i + 1] * ey + headNrm[3 * i + 2] * ez;
        if (d < inward) {
          headNrm[3 * i] = -headNrm[3 * i];
          headNrm[3 * i + 1] = -headNrm[3 * i + 1];
          headNrm[3 * i + 2] = -headNrm[3 * i + 2];
          flippedN++;
        }
      }
      console.log(`  normals: repaired ${flippedN} inward-facing vertex normals`);
    }

    // Spatial blur of the normal field. Graph smoothing only propagates along
    // the strip's own connectivity, so two *overlapping sheets* of the scan (the
    // fold down the centre of the face) keep different normals and shade as a
    // hard seam. Averaging over a small world-space neighbourhood makes
    // co-located surfaces agree regardless of topology, which is exactly what a
    // single smooth surface would do.
    {
      const RADIUS = process.env.HGEN_NRM_BLUR !== undefined ? parseFloat(process.env.HGEN_NRM_BLUR) : 0.09;
      const cell = RADIUS;
      const grid = new Map();
      const key = (x, y, z) => `${Math.floor(x / cell)}_${Math.floor(y / cell)}_${Math.floor(z / cell)}`;
      for (let i = 0; i < nv; ++i) {
        const k = key(headPos[3 * i], headPos[3 * i + 1], headPos[3 * i + 2]);
        let list = grid.get(k);
        if (!list) grid.set(k, (list = []));
        list.push(i);
      }
      const out = new Float32Array(headNrm.length);
      const r2 = RADIUS * RADIUS;
      for (let i = 0; i < nv; ++i) {
        const x = headPos[3 * i], y = headPos[3 * i + 1], z = headPos[3 * i + 2];
        const bx = Math.floor(x / cell), by = Math.floor(y / cell), bz = Math.floor(z / cell);
        let nx = 0, ny = 0, nz = 0, wsum = 0;
        for (let dx = -1; dx <= 1; ++dx)
          for (let dy = -1; dy <= 1; ++dy)
            for (let dz = -1; dz <= 1; ++dz) {
              const list = grid.get(`${bx + dx}_${by + dy}_${bz + dz}`);
              if (!list) continue;
              for (const j of list) {
                const ddx = headPos[3 * j] - x, ddy = headPos[3 * j + 1] - y, ddz = headPos[3 * j + 2] - z;
                const d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                if (d2 > r2) continue;
                // Gaussian falloff, and weight by agreement with the centre
                // vertex so genuine edges (nose, lips) are not melted.
                const w = Math.exp(-d2 / (r2 * 0.35));
                const agree = Math.max(0, headNrm[3 * i] * headNrm[3 * j] +
                  headNrm[3 * i + 1] * headNrm[3 * j + 1] + headNrm[3 * i + 2] * headNrm[3 * j + 2]);
                const ww = w * (0.25 + 0.75 * agree);
                nx += headNrm[3 * j] * ww; ny += headNrm[3 * j + 1] * ww; nz += headNrm[3 * j + 2] * ww;
                wsum += ww;
              }
            }
        if (wsum > 0) {
          const len = Math.hypot(nx, ny, nz) || 1;
          out[3 * i] = nx / len; out[3 * i + 1] = ny / len; out[3 * i + 2] = nz / len;
        } else {
          out[3 * i] = headNrm[3 * i]; out[3 * i + 1] = headNrm[3 * i + 1]; out[3 * i + 2] = headNrm[3 * i + 2];
        }
      }
      headNrm.set(out);
      console.log(`  normals: spatial blur r=${RADIUS}`);
    }
    {
      // Sanity check: after everything, the winding must agree with the shading
      // normals, or half the sheet renders as a back face.
      let agree = 0, disagree = 0;
      for (let t = 0; t < triCount; ++t) {
        const ia = headIdx[3 * t], ib = headIdx[3 * t + 1], ic = headIdx[3 * t + 2];
        const ax = headPos[3 * ia], ay = headPos[3 * ia + 1], az = headPos[3 * ia + 2];
        const ux = headPos[3 * ib] - ax, uy = headPos[3 * ib + 1] - ay, uz = headPos[3 * ib + 2] - az;
        const vx = headPos[3 * ic] - ax, vy = headPos[3 * ic + 1] - ay, vz = headPos[3 * ic + 2] - az;
        const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        let sx = 0, sy = 0, sz = 0;
        for (const i of [ia, ib, ic]) { sx += headNrm[3 * i]; sy += headNrm[3 * i + 1]; sz += headNrm[3 * i + 2]; }
        if (nx * sx + ny * sy + nz * sz >= 0) agree++; else disagree++;
      }
      console.log(`  winding vs shading normals: ${agree} agree, ${disagree} disagree`);
    }
    console.log(`  normals: ${triCount} tris, ${weldId.size} welded verts, ${flipped} flipped outwards, ` +
                `${nrmPasses} smoothing passes, ${degenerate} degenerate groups repaired`);
  }

  if (process.env.HGEN_ORIGINAL_NORMALS) {
    for (let i = 0; i < headNrm.length; ++i) headNrm[i] = origNrm[i];
    console.log('  normals: demo obj_normals (compare only)');
  }

console.log(`  neck cut y=${cut.toFixed(2)} r=${radius.toFixed(2)}: kept ${kept.length / 3}, dropped ${dropped}`);
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

/* ---------------------------------------------------------------- plinth */
// The cut neck is finished with a tapered pedestal so the portrait reads as a
// bust instead of a mesh with the body torn off. Everything here is generated
// (the demo has no such geometry) but it is driven by the cut ring's measured
// radius, and it is what lets the head sit on the ground plane with a shadow.

const plinth = (() => {
  // Measure the neck: mean centre and radius of the vertices just below the jaw.
  // Only vertices that survived the neck cut count, otherwise the shoulder
  // mass would inflate the radius.
  const neckVerts = new Set();
  for (const v of headIdx) {
    const y = headPos[3 * v + 1];
    if (y <= -0.24 && y >= -0.34) neckVerts.add(v);
  }
  let cx = 0, cz = 0, maxR = 0;
  for (const v of neckVerts) { cx += headPos[3 * v]; cz += headPos[3 * v + 2]; }
  if (neckVerts.size === 0) return null;
  cx /= neckVerts.size; cz /= neckVerts.size;
  for (const v of neckVerts)
    maxR = Math.max(maxR, Math.hypot(headPos[3 * v] - cx, headPos[3 * v + 2] - cz));
  const SEG = 64;
  const topR = Math.max(0.40, maxR * 1.02);
  // A tapered pedestal from just above the neck cut down to the ground plane.
  const rings = [
    [-0.285, topR * 1.0],
    [-0.31, topR * 1.01],
    [-0.35, topR * 0.92],
    [-0.40, topR * 0.86],
    [-0.45, topR * 0.84],
    [-0.48, topR * 0.84],
  ];
  const pos = [], nrm = [], idx = [];
  for (const [y, r] of rings) {
    for (let i = 0; i < SEG; ++i) {
      const a = (i / SEG) * Math.PI * 2;
      pos.push(cx + Math.cos(a) * r, y, cz + Math.sin(a) * r);
      nrm.push(Math.cos(a), 0.06, Math.sin(a));
    }
  }
  for (let ring = 0; ring + 1 < rings.length; ++ring) {
    for (let i = 0; i < SEG; ++i) {
      const a = ring * SEG + i, b = ring * SEG + ((i + 1) % SEG);
      const c2 = (ring + 1) * SEG + i, d = (ring + 1) * SEG + ((i + 1) % SEG);
      idx.push(a, c2, b, b, c2, d);
    }
  }
  // Bottom cap (faces down, only visible from below).
  const baseY = rings[rings.length - 1][0];
  const centre = pos.length / 3;
  pos.push(cx, baseY, cz);
  nrm.push(0, -1, 0);
  const last = (rings.length - 1) * SEG;
  for (let i = 0; i < SEG; ++i) idx.push(centre, last + ((i + 1) % SEG), last + i);
  console.log(`  plinth: neck centre (${cx.toFixed(3)}, ${cz.toFixed(3)}), radius ${topR.toFixed(3)}, ${idx.length / 3} tris`);
  return { pos, nrm, idx, topY: rings[0][0] };
})();

if (plinth) {
  addChunk('bust.pos', plinth.pos.length / 3, 1, 1, 3, 2, f32(plinth.pos));
  addChunk('bust.nrm', plinth.nrm.length / 3, 1, 1, 3, 2, f32(plinth.nrm));
  addChunk('bust.idx', plinth.idx.length, 1, 1, 1, 3, u32(plinth.idx));
}



console.log('hair mesh texture: %dx%dx%d, %d levels', tex_hairmesh_width, tex_hairmesh_height, tex_hairmesh_depth, maxLevel + 1);

addChunk('head.pos', headPos.length / 3, 1, 1, 3, 2, f32(headPos));
addChunk('head.nrm', headNrm.length / 3, 1, 1, 3, 2, f32(headNrm));
addChunk('head.uv', headUV.length / 2, 1, 1, 2, 2, f32(headUV));
addChunk('head.idx', headIdx.length, 1, 1, 1, 3, u32(headIdx));

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
// Every material ships its own set of Marschner lookup tables. `std_blond` is
// the demo checkout's root PNGs; other sets live in materials/<name>/.
const MATERIALS = ['std_blond', 'rosa'];
const materialLutPath = (name, f) => {
  const candidates = [
    path.join(SRC, 'materials', name, f),
    path.join(SRC, f),
  ];
  for (const c of candidates) if (fs.existsSync(c)) return c;
  throw new Error(`lookup table not found for material ${name}: ${f}`);
};

// --- LUT materials --------------------------------------------------------
// `std_blond` is the demo checkout's root PNGs and defines the lookup tables'
// angular structure. The `materials/rosa` PNGs were authored by recolouring
// them, which shifted their *brightness* as well as their hue: the rosa N_TRT
// table is ~5x brighter than the blond one, so the TRT lobe swamps R and TT and
// the whole head flattens into one pink mass with no fibre structure.
//
// Re-derive every table so it keeps the blond set's per-texel luminance (the
// scattering structure, which is physically what those tables encode) and takes
// only its *chroma* from the supplied rosa set. Multiple-scattering tables are
// desaturated, because light that has bounced many times inside the fibre loses
// its chromatic memory.
function chroma(r, g, b) {
  const m = Math.max(r, g, b, 1e-3);
  return [r / m, g / m, b / m];
}
function meanLuma(data, comps, channels) {
  let acc = 0;
  const n = data.length / comps;
  for (let i = 0; i < n; ++i) {
    const o = i * comps;
    acc += 0.2126 * data[o] + 0.7152 * data[o + 1] + 0.0722 * data[o + 2];
  }
  return acc / Math.max(1, channels);
}
function recolorTable(base, tint, comps, desaturate) {
  // `base` supplies structure, `tint` supplies hue.
  const out = Uint8Array.from(base.data);
  const baseLuma = [], tintLuma = [];
  const n = base.data.length / comps;
  // Reference chroma per table = the tint's own mean chroma; using the mean
  // rather than per-texel ratios keeps the base's angular variation intact.
  let tr = 0, tg = 0, tb = 0;
  for (let i = 0; i < n; ++i) {
    const o = i * comps;
    const c = chroma(tint.data[o], tint.data[o + 1], tint.data[o + 2]);
    tr += c[0]; tg += c[1]; tb += c[2];
  }
  tr /= n; tg /= n; tb /= n;
  let br = 0, bg = 0, bb = 0;
  for (let i = 0; i < n; ++i) {
    const o = i * comps;
    const c = chroma(base.data[o], base.data[o + 1], base.data[o + 2]);
    br += c[0]; bg += c[1]; bb += c[2];
  }
  br /= n; bg /= n; bb /= n;
  const ratio = [tr / Math.max(br, 1e-3), tg / Math.max(bg, 1e-3), tb / Math.max(bb, 1e-3)]
    .map((x) => 1 + (Math.min(4, Math.max(0.25, x)) - 1) * (1 - desaturate));
  for (let i = 0; i < n; ++i) {
    const o = i * comps;
    if (comps >= 3) {
      out[o] = Math.min(255, Math.round(base.data[o] * ratio[0]));
      out[o + 1] = Math.min(255, Math.round(base.data[o + 1] * ratio[1]));
      out[o + 2] = Math.min(255, Math.round(base.data[o + 2] * ratio[2]));
    }
    if (comps === 4) out[o + 3] = base.data[o + 3];  // N_R alpha stays scalar
  }
  return { w: base.w, h: base.h, d: base.d, data: out };
}

// name, luminance-preserving? , desaturation of the chroma transfer
const LUT_SPEC = [
  ['N_TT_R', 4, 2, 0.0],
  ['N_TRT', 3, 2, 0.0],
  ['N_TT_R_GI', 4, 2, 0.35],
  ['N_TRT_GI', 3, 2, 0.35],
  ['GI', 3, 3, 0.55],
  ['M_GI', 3, 2, 0.35],
];

for (const mat of MATERIALS) {
  const tag = `mtl.${mat}.`;
  const ref = mat !== 'std_blond';   // every non-blond set is derived from blond
  for (const [lutName, comps, dim, desat] of LUT_SPEC) {
    const base = lut(materialLutPath('std_blond', lutName + '.png'), comps, dim);
    let table = base;
    if (ref) {
      const tint = lut(materialLutPath(mat, lutName + '.png'), comps, dim);
      table = recolorTable(base, tint, comps, desat);
    }
    addChunk(tag + lutName, table.w, table.h, table.d, comps, 0, u8(table.data));
  }
  const probe = lut(materialLutPath(mat, 'N_TT_R.png'), 4, 2);
  console.log(`  material ${mat}: LUTs ${probe.w}², GI ${lut(materialLutPath(mat, 'GI.png'), 3, 3).w}³` +
              (ref ? ' (re-derived: blond structure, supplied hue)' : ''));
}
// The material table so the runtime does not have to hard-code the names.
const matNames = MATERIALS.map((m) => m + '\0').join('') + '\0';
addChunk('mtl.names', matNames.length, 1, 1, 1, 0, Buffer.from(matNames, 'ascii'));

// skin albedo (july.jpg) - downsampled to 1024^2 RGB
const skin = loadImage(path.join(SRC, 'july.jpg'));
const skinRGB = new Uint8Array(skin.width * skin.height * 3);
for (let i = 0; i < skin.width * skin.height; ++i) {
  skinRGB[3 * i] = skin.data[4 * i];
  skinRGB[3 * i + 1] = skin.data[4 * i + 1];
  skinRGB[3 * i + 2] = skin.data[4 * i + 2];
}
// The photo is a UV unwrap on a black background: wherever the mesh's UVs fall
// outside the island the sample is black, which rendered as holes in the face.
// Dilate the island outwards (inpainting) so every UV has skin under it.
{
  const W = skin.width, H = skin.height;
  const valid = new Uint8Array(W * H);
  for (let i = 0; i < W * H; ++i)
    valid[i] = (skinRGB[3 * i] + skinRGB[3 * i + 1] + skinRGB[3 * i + 2]) > 24 ? 1 : 0;
  let filled = 0;
  for (let pass = 0; pass < 24; ++pass) {
    const next = valid.slice();
    let changed = 0;
    for (let y = 0; y < H; ++y)
      for (let x = 0; x < W; ++x) {
        const i = y * W + x;
        if (next[i]) continue;
        let r = 0, g = 0, b = 0, n = 0;
        for (let dy = -1; dy <= 1; ++dy)
          for (let dx = -1; dx <= 1; ++dx) {
            const nx2 = x + dx, ny2 = y + dy;
            if (nx2 < 0 || ny2 < 0 || nx2 >= W || ny2 >= H) continue;
            const j = ny2 * W + nx2;
            if (!valid[j]) continue;
            r += skinRGB[3 * j]; g += skinRGB[3 * j + 1]; b += skinRGB[3 * j + 2]; n++;
          }
        if (n === 0) continue;
        skinRGB[3 * i] = Math.round(r / n);
        skinRGB[3 * i + 1] = Math.round(g / n);
        skinRGB[3 * i + 2] = Math.round(b / n);
        next[i] = 2;  // filled this pass, usable by the next one
        changed++;
      }
    for (let i = 0; i < W * H; ++i) if (next[i]) valid[i] = 1;
    filled += changed;
    if (!changed) break;
  }
  console.log(`  skin albedo: inpainted ${filled} texels around the UV island`);
}
addChunk('skin.albedo', skin.width, skin.height, 1, 3, 0, u8(skinRGB));

/* ------------------------------------------------- head detail maps */
// Optional detail maps that ship next to the demo (textures/). They are plain
// 1024^2 images: a tangent-space normal map plus grayscale height, ambient
// occlusion and specular masks.
function detailMap(file, comps) {
  const full = path.join(SRC, 'textures', file);
  if (!fs.existsSync(full)) return null;
  const img = loadImage(full);
  const out = new Uint8Array(img.width * img.height * comps);
  for (let i = 0; i < img.width * img.height; ++i)
    for (let c = 0; c < comps; ++c) out[i * comps + c] = img.data[4 * i + Math.min(c, 2)];
  return { w: img.width, h: img.height, data: out, comps };
}

// --- skin detail: normal map -------------------------------------------
// The supplied `july_normal.png` is a *pore* detail map: a smooth base with one
// texel of noise on top (mean |dN| is huge). Sampled 1:1 across a UV island that
// is stretched over a whole face it renders as gravel, so it is only usable after
// heavy prefiltering. The supplied height map is clean, so the relief we ship is
// derived from it (Sobel -> tangent space) and the noise map is blended in at a
// small weight for micro-detail.
function boxDown(data, w, h, comps, factor) {
  const ow = Math.max(1, Math.floor(w / factor)), oh = Math.max(1, Math.floor(h / factor));
  const out = new Float32Array(ow * oh * comps);
  for (let y = 0; y < oh; ++y)
    for (let x = 0; x < ow; ++x) {
      for (let c = 0; c < comps; ++c) {
        let acc = 0, n = 0;
        for (let j = 0; j < factor; ++j)
          for (let i = 0; i < factor; ++i) {
            const px = Math.min(w - 1, x * factor + i), py = Math.min(h - 1, y * factor + j);
            acc += data[comps * (py * w + px) + c];
            n++;
          }
        out[comps * (y * ow + x) + c] = acc / n;
      }
    }
  return { w: ow, h: oh, data: out };
}
function upBilinear(src, w, h, comps) {
  const out = new Float32Array(w * h * comps);
  const sx = src.w / w, sy = src.h / h;
  for (let y = 0; y < h; ++y)
    for (let x = 0; x < w; ++x) {
      const fx = Math.min(src.w - 1.001, Math.max(0, (x + 0.5) * sx - 0.5));
      const fy = Math.min(src.h - 1.001, Math.max(0, (y + 0.5) * sy - 0.5));
      const x0 = Math.floor(fx), y0 = Math.floor(fy), tx = fx - x0, ty = fy - y0;
      for (let c = 0; c < comps; ++c) {
        const a = src.data[comps * (y0 * src.w + x0) + c];
        const b = src.data[comps * (y0 * src.w + x0 + 1) + c];
        const d = src.data[comps * ((y0 + 1) * src.w + x0) + c];
        const e = src.data[comps * ((y0 + 1) * src.w + x0 + 1) + c];
        out[comps * (y * w + x) + c] = (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty;
      }
    }
  return out;
}

const heightMap = detailMap('july_height.png', 1);
const noiseNormal = detailMap('july_normal.png', 3);
if (heightMap) {
  const W = 512, H = 512;                      // shipped resolution
  // Sobel the height map into tangent space, at the shipped resolution.
  const hSrc = upBilinear({ w: heightMap.w, h: heightMap.h, data: Float32Array.from(heightMap.data) },
                          W, H, 1);
  const out = new Uint8Array(W * H * 4);
  const STRENGTH = parseFloat(process.env.HGEN_HEIGHT_STRENGTH || '7');
  // High-pass the height field: the big relief (nose, lips, brow) is already in
  // the mesh, and shading it twice gives blobby facets. Subtracting a blurred copy
  // leaves only wrinkle/pore relief, which is what a detail normal map should be.
  {
    const blurred = upBilinear(boxDown(hSrc, W, H, 1, 24), W, H, 1);
    for (let i = 0; i < W * H; ++i) hSrc[i] = Math.max(0, Math.min(255, hSrc[i] - blurred[i] + 128));
  }
  const at = (x, y) => hSrc[Math.min(H - 1, Math.max(0, y)) * W + Math.min(W - 1, Math.max(0, x))];
  for (let y = 0; y < H; ++y)
    for (let x = 0; x < W; ++x) {
      const dhdu = (at(x + 1, y) - at(x - 1, y)) * 0.5;
      const dhdv = (at(x, y - 1) - at(x, y + 1)) * 0.5;   // +v is up, rows run down
      let nx = -dhdu / 255 * STRENGTH, ny = -dhdv / 255 * STRENGTH, nz = 1;
      const len = Math.hypot(nx, ny, nz);
      nx /= len; ny /= len; nz /= len;
      const o = 4 * (y * W + x);
      out[o] = Math.round((nx * 0.5 + 0.5) * 255);
      out[o + 1] = Math.round((ny * 0.5 + 0.5) * 255);
      out[o + 2] = Math.round((nz * 0.5 + 0.5) * 255);
      out[o + 3] = 255;
    }
  if (noiseNormal) {
    // Keep a whisper of the pore noise: 8x box filter, then blend 25%.
    const coarse = boxDown(noiseNormal.data, noiseNormal.w, noiseNormal.h, 3, 8);
    const up = upBilinear(coarse, W, H, 3);
    for (let i = 0; i < W * H; ++i) {
      const hx = out[4 * i] / 127.5 - 1, hy = out[4 * i + 1] / 127.5 - 1, hz = out[4 * i + 2] / 127.5 - 1;
      // Authored Y-down; flip G for the Y-up TBN the shader builds.
      const mx = up[3 * i] / 127.5 - 1, my = -(up[3 * i + 1] / 127.5 - 1), mz = up[3 * i + 2] / 127.5 - 1;
      let nx = hx * 0.8 + mx * 0.2, ny = hy * 0.8 + my * 0.2, nz = hz * 0.8 + mz * 0.2;
      const len = Math.hypot(nx, ny, nz) || 1;
      out[4 * i] = Math.round((nx / len * 0.5 + 0.5) * 255);
      out[4 * i + 1] = Math.round((ny / len * 0.5 + 0.5) * 255);
      out[4 * i + 2] = Math.round((nz / len * 0.5 + 0.5) * 255);
    }
  }
  addChunk('skin.normal', W, H, 1, 4, 0, u8(out));
  console.log(`  skin.normal: ${W}x${H} (height-derived + prefiltered pores)`);
}
for (const [file, name] of [['july_ao.png', 'skin.ao'],
                            ['july_spec.png', 'skin.spec']]) {
  const m = detailMap(file, 1);
  if (!m) continue;
  addChunk(name, m.w, m.h, 1, 1, 0, u8(m.data));
  console.log(`  ${name}: ${m.w}x${m.h}`);
}

// Low-frequency copy of the albedo. The photo carries its own baked lighting
// (dark eye sockets, hair shadow); dividing by this in the shader flattens that
// out so the renderer's own lighting reads instead of the photograph's.
const FLAT = 48;
const flat = new Uint8Array(FLAT * FLAT * 3);
{
  const sx = skin.width / FLAT, sy = skin.height / FLAT;
  for (let y = 0; y < FLAT; ++y)
    for (let x = 0; x < FLAT; ++x) {
      const acc = [0, 0, 0];
      let n = 0;
      for (let j = 0; j < sy; ++j)
        for (let i = 0; i < sx; ++i) {
          const px = Math.min(skin.width - 1, Math.floor(x * sx + i));
          const py = Math.min(skin.height - 1, Math.floor(y * sy + j));
          const o = 3 * (py * skin.width + px);
          acc[0] += skinRGB[o]; acc[1] += skinRGB[o + 1]; acc[2] += skinRGB[o + 2];
          n++;
        }
      const o = 3 * (y * FLAT + x);
      for (let c = 0; c < 3; ++c) flat[o + c] = Math.round(acc[c] / n);
    }
}
addChunk('skin.flat', FLAT, FLAT, 1, 3, 0, u8(flat));

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
