#!/usr/bin/env node
/*
 * export_glb.js - writes the July head out as a self-contained glTF 2.0 binary
 * (assets/July_Head.glb): the mesh the renderer draws, plus every skin map in
 * the bundle, embedded as PNG.
 *
 *   node tools/export_glb.js [--assets assets/hairgen_assets.bin]
 *                            [--out assets/July_Head.glb]
 *                            [--height ../textures/july_height.png]
 *
 * The geometry is read from the asset bundle rather than re-derived from
 * july-low.js, so the GLB is by construction the same head the renderer draws:
 * same centring, same scale, same neck cut, same winding, same normals. The
 * bundle stores the maps as raw texels (the renderer uploads them straight into
 * R8 textures), so they are re-encoded to PNG here - losslessly, and without any
 * re-filtering or resampling, because a GLB of the head is only useful if it
 * carries the renderer's own pixels.
 *
 * Texture roles follow the glTF 2.0 material model:
 *
 *   baseColorTexture          skin.albedo   (july.jpg, UV island inpainted)
 *   normalTexture             skin.normal   (height-derived pores, tangent space)
 *   occlusionTexture       R  skin.ao
 *   metallicRoughnessTexture  G = 255 - skin.spec, B = 0 (skin is dielectric)
 *
 * AO and roughness live in one RGB image (R/G/B = occlusion/roughness/metallic,
 * the usual "ORM" packing) so the material needs one texture for both. The
 * height map has no slot in core glTF; it is embedded anyway, unreferenced by
 * the material and named in the document extras, because the normal map here is
 * derived from it and a round trip back into the exporter needs it.
 */
'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

/* ------------------------------------------------------------------ args */

const args = process.argv.slice(2);
function arg(name, def) {
  const i = args.indexOf('--' + name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
}
const OUT = path.resolve(arg('out', path.join(__dirname, '..', 'assets', 'July_Head.glb')));
const ASSETS = path.resolve(arg('assets', path.join(__dirname, '..', 'assets', 'hairgen_assets.bin')));
const HEIGHT_PNG = path.resolve(
  arg('height', path.join(__dirname, '..', '..', 'textures', 'july_height.png')));

/* ------------------------------------------------- asset bundle (HGENAST2) */

// Container format: 16-byte header, then a 64-byte record per chunk. The record
// carries the chunk's dimensions/format, its (null-terminated) name and an
// absolute file offset + size. Same layout as src/assets.cpp::Assets::load().
function readBundle(file) {
  const buf = fs.readFileSync(file);
  if (buf.toString('ascii', 0, 8) !== 'HGENAST2')
    throw new Error('bad asset bundle magic: ' + file);
  const count = buf.readUInt32LE(8);
  const chunks = new Map();
  for (let i = 0, off = 16; i < count; ++i, off += 64) {
    const c = {
      width: buf.readUInt32LE(off),
      height: buf.readUInt32LE(off + 4),
      depth: buf.readUInt32LE(off + 8),
      channels: buf.readUInt32LE(off + 12),
      format: buf.readUInt32LE(off + 16),   // 0 u8, 1 u16, 2 f32, 3 u32
      name: buf.toString('ascii', off + 20, off + 48).replace(/\0.*$/, ''),
    };
    const dataOff = Number(buf.readBigUInt64LE(off + 48));
    const size = Number(buf.readBigUInt64LE(off + 56));
    if (dataOff + size > buf.length) throw new Error('truncated chunk: ' + c.name);
    c.data = buf.subarray(dataOff, dataOff + size);
    chunks.set(c.name, c);
  }
  return chunks;
}

const get = (chunks, name) => {
  const c = chunks.get(name);
  if (!c) throw new Error('asset bundle has no chunk "' + name + '"');
  return c;
};

// Features are f32/u32; wrap a chunk as a typed array over its slice.
function floats(c) {
  if (c.format !== 2) throw new Error(`${c.name}: expected f32`);
  return new Float32Array(c.data.buffer.slice(c.data.byteOffset, c.data.byteOffset + c.data.length));
}
function ints(c) {
  if (c.format !== 3) throw new Error(`${c.name}: expected u32`);
  return new Uint32Array(c.data.buffer.slice(c.data.byteOffset, c.data.byteOffset + c.data.length));
}

/* --------------------------------------------------------- png encoding */

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; ++n) {
    let c = n;
    for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; ++i) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function pngChunk(kind, data) {
  const head = Buffer.alloc(8);
  head.writeUInt32BE(data.length, 0);
  head.write(kind, 4, 'ascii');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4, 8), data])), 0);
  return Buffer.concat([head, data, crc]);
}

// Encode 8-bit grayscale (1) / RGB (3) / RGBA (4) texels as a PNG. Filter type 0
// on every row: these are photographs and detail maps that zlib compresses
// well anyway, and staying unfiltered keeps the encoder dependency-free.
function encodePNG(width, height, channels, texels) {
  const colorType = { 1: 0, 2: 4, 3: 2, 4: 6 }[channels];
  if (colorType === undefined) throw new Error('unsupported channel count ' + channels);
  const stride = width * channels;
  if (texels.length < stride * height) throw new Error('short texel buffer');
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; ++y) {
    raw[y * (stride + 1)] = 0;
    Buffer.from(texels.buffer, texels.byteOffset + y * stride, stride)
      .copy(raw, y * (stride + 1) + 1);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;              // bit depth
  ihdr[9] = colorType;
  ihdr[10] = 0;             // deflate
  ihdr[11] = 0;             // adaptive filtering
  ihdr[12] = 0;             // no interlace
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

/* ------------------------------------------------------------- geometry */

const chunks = readBundle(ASSETS);

const pos = floats(get(chunks, 'head.pos'));
const nrm = floats(get(chunks, 'head.nrm'));
const uv = floats(get(chunks, 'head.uv'));
const idx = ints(get(chunks, 'head.idx'));

const vertexCount = pos.length / 3;
const triCount = idx.length / 3;
if (nrm.length !== vertexCount * 3) throw new Error('head.nrm vertex count mismatch');
if (uv.length !== vertexCount * 2) throw new Error('head.uv vertex count mismatch');
if (pos.length % 3 || idx.length % 3) throw new Error('head geometry is not 3-component');

const bounds = {
  min: [Infinity, Infinity, Infinity],
  max: [-Infinity, -Infinity, -Infinity],
};
for (let i = 0; i < pos.length; i += 3)
  for (let k = 0; k < 3; ++k) {
    bounds.min[k] = Math.min(bounds.min[k], pos[i + k]);
    bounds.max[k] = Math.max(bounds.max[k], pos[i + k]);
  }

// glTF/OpenGL default front face is counter-clockwise seen from outside, which
// is also what the renderer culls against (VK_FRONT_FACE_COUNTER_CLOCKWISE,
// BACK cull). Rather than trust that, check it against the vertex normals: if the
// geometric normal of the triangles disagrees with them, the winding is inverted
// and every viewer would show the head inside out.
let agree = 0;
for (let t = 0; t < idx.length; t += 3) {
  const a = idx[t] * 3, b = idx[t + 1] * 3, c = idx[t + 2] * 3;
  const e1 = [pos[b] - pos[a], pos[b + 1] - pos[a + 1], pos[b + 2] - pos[a + 2]];
  const e2 = [pos[c] - pos[a], pos[c + 1] - pos[a + 1], pos[c + 2] - pos[a + 2]];
  const g = [
    e1[1] * e2[2] - e1[2] * e2[1],
    e1[2] * e2[0] - e1[0] * e2[2],
    e1[0] * e2[1] - e1[1] * e2[0],
  ];
  const n = [nrm[a] + nrm[b] + nrm[c], nrm[a + 1] + nrm[b + 1] + nrm[c + 1],
             nrm[a + 2] + nrm[b + 2] + nrm[c + 2]];
  if (g[0] * n[0] + g[1] * n[1] + g[2] * n[2] >= 0) ++agree;
}
const clockwise = agree * 2 < triCount;
const indices = new Uint32Array(idx.length);
if (clockwise) {
  for (let t = 0; t < idx.length; t += 3) {
    indices[t] = idx[t];
    indices[t + 1] = idx[t + 2];
    indices[t + 2] = idx[t + 1];
  }
} else {
  indices.set(idx);
}

/* ------------------------------------------------------------- textures */

const albedo = get(chunks, 'skin.albedo');
const normal = get(chunks, 'skin.normal');
const ao = get(chunks, 'skin.ao');
const spec = get(chunks, 'skin.spec');
const flat = get(chunks, 'skin.flat');

// Re-pack a chunk's texels to `picks` channels (indices into the stored ones):
// the bundle keeps the normal map as RGBA, glTF wants RGB, and the alpha is only
// an auxiliary height there.
function pack(chunk, picks) {
  const w = chunk.width, h = chunk.height;
  if (w * h * chunk.channels !== chunk.data.length)
    throw new Error(`${chunk.name}: data is not w*h*channels`);
  const out = new Uint8Array(w * h * picks.length);
  for (let i = 0; i < w * h; ++i)
    for (let k = 0; k < picks.length; ++k) {
      const src = picks[k];
      if (src >= chunk.channels) throw new Error(`${chunk.name}: no channel ${src}`);
      out[i * picks.length + k] = chunk.data[i * chunk.channels + src];
    }
  return out;
}
const texPng = (chunk, picks) =>
  encodePNG(chunk.width, chunk.height, picks.length, pack(chunk, picks));

const images = [];
function addImage(name, png) {
  images.push({ name, png });
  return images.length - 1;
}

const iBaseColor = addImage('July_Head_BaseColor', texPng(albedo, [0, 1, 2]));
const iNormal = addImage('July_Head_Normal', texPng(normal, [0, 1, 2]));
const iHeight = fs.existsSync(HEIGHT_PNG)
  ? addImage('July_Head_Height', fs.readFileSync(HEIGHT_PNG))
  : -1;

// ORM: R = ambient occlusion, G = roughness, B = metallic. The bundle's spec map
// is a *gloss* mask (the shader mixes 0.35..1.0 of it into the highlight), so it
// becomes 255 - spec; skin is a dielectric, so metallic stays 0.
{
  const w = ao.width, h = ao.height;
  if (spec.width !== w || spec.height !== h) throw new Error('skin.ao / skin.spec size mismatch');
  const orm = new Uint8Array(w * h * 3);
  for (let i = 0; i < w * h; ++i) {
    orm[3 * i] = ao.data[i];
    orm[3 * i + 1] = 255 - spec.data[i];
    orm[3 * i + 2] = 0;
  }
  addImage('July_Head_ORM', encodePNG(w, h, 3, orm));
}
const iORM = images.length - 1;

const flatColor = [flat.data[0] / 255, flat.data[1] / 255, flat.data[2] / 255];

/* ------------------------------------------------------------------ glb */

const align4 = (n) => (n + 3) & ~3;
const bin = [];
let binLength = 0;
function appendBuffer(buf, alignment = 4) {
  const pad = (alignment - (binLength % alignment)) % alignment;
  if (pad) {
    bin.push(Buffer.alloc(pad));
    binLength += pad;
  }
  const offset = binLength;
  bin.push(buf);
  binLength += buf.length;
  return offset;
}

const bufferViews = [];
function addView(buf, target) {
  const byteOffset = appendBuffer(buf);
  const view = { buffer: 0, byteOffset, byteLength: buf.length };
  if (target) view.target = target;
  bufferViews.push(view);
  return bufferViews.length - 1;
}

const asBytes = (typed) => Buffer.from(typed.buffer, typed.byteOffset, typed.byteLength);
const vPos = addView(asBytes(pos), 34962);
const vNrm = addView(asBytes(nrm), 34962);
const vUV = addView(asBytes(uv), 34962);

// 16-bit indices when they fit: half the bytes for a 3 k-vertex head.
const useU16 = vertexCount <= 65535;
const indexData = useU16 ? Uint16Array.from(indices) : indices;
const vIdx = addView(asBytes(indexData), 34963);

const imageViews = images.map((img) => addView(img.png));

const accessors = [
  {
    bufferView: vPos, componentType: 5126, count: vertexCount, type: 'VEC3',
    min: bounds.min, max: bounds.max,
  },
  { bufferView: vNrm, componentType: 5126, count: vertexCount, type: 'VEC3' },
  { bufferView: vUV, componentType: 5126, count: vertexCount, type: 'VEC2' },
  {
    bufferView: vIdx, componentType: useU16 ? 5123 : 5125, count: indices.length,
    type: 'SCALAR',
  },
];

const gltf = {
  asset: {
    version: '2.0',
    generator: 'HairGenVulkan tools/export_glb.js',
  },
  scene: 0,
  scenes: [{ name: 'July', nodes: [0] }],
  nodes: [{ name: 'July_Head', mesh: 0 }],
  meshes: [{
    name: 'July_Head',
    primitives: [{
      attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 },
      indices: 3,
      material: 0,
      mode: 4,                       // TRIANGLES
    }],
  }],
  materials: [{
    name: 'July_Head_Skin',
    doubleSided: false,
    pbrMetallicRoughness: {
      baseColorTexture: { index: 0 },
      metallicFactor: 0.0,
      roughnessFactor: 1.0,
      metallicRoughnessTexture: { index: 1 },
    },
    normalTexture: { index: 2, scale: 1.0 },
    occlusionTexture: { index: 1, strength: 1.0 },
    extras: {
      rendererMapping:
        'R = skin.ao (occlusion), G = 255 - skin.spec (gloss mask inverted to ' +
        'roughness), B = 0. The renderer squares the AO and mixes the spec mask ' +
        'as 0.35..1.0 of the sun highlight in shaders/head.frag.',
      flatColor: flatColor.map((v) => Math.round(v * 1000) / 1000),
    },
  }],
  // Two texture entries: the ORM image is sampled twice, which is legal and
  // keeps the occlusion and roughness slots independent for editing.
  textures: [
    { sampler: 0, source: iBaseColor },
    { sampler: 0, source: iORM },
    { sampler: 0, source: iNormal },
  ],
  samplers: [{
    magFilter: 9729,            // LINEAR
    minFilter: 9987,            // LINEAR_MIPMAP_LINEAR
    wrapS: 33071,               // CLAMP_TO_EDGE
    wrapT: 33071,
  }],
  images: images.map((img, i) => ({
    name: img.name,
    mimeType: 'image/png',
    bufferView: imageViews[i],
  })),
  bufferViews,
  accessors,
  buffers: [{ byteLength: 0 }],
  extras: {
    source: 'july-low.js (obj_vert_pos / obj_normals / obj_texCoords / obj_indices) ' +
            'through assets/hairgen_assets.bin - the same transformed head the ' +
            'Vulkan renderer draws',
    units: '1 unit = 1 renderer unit: the head is normalised to 2 units on its ' +
           'largest axis and shifted by (0, -0.2, 0), see tools/export_assets.js',
    upAxis: '+Y',
    heightTexture: iHeight,       // not referenced by the material: glTF has no slot
    heightNote: 'skin.albedo/normal/ao/spec are the renderer bundle texels, ' +
                're-encoded losslessly; July_Head_Height.png is the supplied PNG ' +
                'the normal map was derived from',
  },
};

// buffer[0] is the BIN chunk itself, so its length is known after everything is
// appended; the JSON stringifies once at the end with that number filled in.
const binBuffer = Buffer.concat(bin.map((b) => Buffer.from(b)));
gltf.buffers[0].byteLength = binBuffer.length;

let json = Buffer.from(JSON.stringify(gltf), 'utf8');
if (json.length % 4) json = Buffer.concat([json, Buffer.alloc(4 - (json.length % 4), 0x20)]);
const binPadded = binBuffer.length % 4
  ? Buffer.concat([binBuffer, Buffer.alloc(4 - (binBuffer.length % 4))])
  : binBuffer;

const total = 12 + 8 + json.length + 8 + binPadded.length;
const header = Buffer.alloc(12);
header.write('glTF', 0, 'ascii');
header.writeUInt32LE(2, 4);
header.writeUInt32LE(total, 8);
const jsonHeader = Buffer.alloc(8);
jsonHeader.writeUInt32LE(json.length, 0);
jsonHeader.write('JSON', 4, 'ascii');
const binHeader = Buffer.alloc(8);
binHeader.writeUInt32LE(binPadded.length, 0);
binHeader.write('BIN\0', 4, 'ascii');

fs.mkdirSync(path.dirname(OUT), { recursive: true });
fs.writeFileSync(OUT, Buffer.concat([header, jsonHeader, json, binHeader, binPadded]));

console.log(`  head: ${vertexCount} verts, ${triCount} tris, ` +
            `${useU16 ? 'u16' : 'u32'} indices, winding ${clockwise ? 'reversed' : 'kept'}`);
console.log(`  bounds: [${bounds.min.map((v) => v.toFixed(3))}] .. ` +
            `[${bounds.max.map((v) => v.toFixed(3))}]`);
for (const img of images)
  console.log(`  texture: ${img.name}.png ${img.png.length} bytes`);
console.log(`export_glb: wrote ${path.relative(process.cwd(), OUT)} ` +
            `(${(total / 1048576).toFixed(2)} MiB)`);
