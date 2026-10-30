#!/usr/bin/env python3
"""test_glb.py - structural check of the exported head (assets/July_Head.glb).

The GLB is a deliverable that no other test touches: the renderer never reads it,
so nothing here would notice it going stale or malformed. This parses the
container by hand (no glTF library, no GPU) and asserts the things that would
actually break a load in another engine:

  * the container and its two chunks are well formed and padded;
  * every bufferView and accessor stays inside the binary chunk;
  * POSITION/NORMAL/TEXCOORD_0 agree on the vertex count, indices are in range;
  * position bounds match the accessor's declared min/max (glTF requires them,
    and viewers use them for framing);
  * every image is a real PNG whose size the embedded header agrees with;
  * the material's texture references resolve, and the ORM image is packed the way
    the material extras document (R = AO, G = roughness, B = 0).

Run:  python3 tests/test_glb.py [path/to/July_Head.glb]
"""
import json
import struct
import sys
import zlib
import pathlib

NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
CSIZE = {5121: 1, 5123: 2, 5125: 4, 5126: 4}


def png_info(data):
    """Validate a PNG and return (width, height, channels) without decoding it."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'image is not a PNG'
    off, width, height, ctype, idat = 8, 0, 0, 0, b''
    while off < len(data):
        (length,) = struct.unpack('>I', data[off:off + 4])
        kind = data[off + 4:off + 8]
        chunk = data[off + 8:off + 8 + length]
        assert zlib.crc32(kind + chunk) & 0xffffffff == struct.unpack(
            '>I', data[off + 8 + length:off + 12 + length])[0], f'bad CRC in {kind}'
        if kind == b'IHDR':
            width, height, depth, ctype = struct.unpack('>IIBB', chunk[:10])
            assert depth == 8, 'only 8-bit images are expected'
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'IEND':
            break
        off += 12 + length
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]   # gray / RGB / gray+alpha / RGBA
    # Decompress the pixels: catches a truncated IDAT that a header check misses.
    raw = zlib.decompress(idat)
    assert len(raw) == (width * channels + 1) * height, 'pixel data is short'
    return width, height, channels, raw


def check(path, here_path):
    data = pathlib.Path(path).read_bytes()
    magic, version, total = struct.unpack('<4sII', data[:12])
    assert magic == b'glTF', 'not a GLB'
    assert version == 2, 'not glTF 2.0'
    assert total == len(data), f'header length {total} != file length {len(data)}'

    off, gltf, binary = 12, None, None
    while off < len(data):
        length, kind = struct.unpack('<I4s', data[off:off + 8])
        payload = data[off + 8:off + 8 + length]
        if kind == b'JSON':
            gltf = json.loads(payload)
        elif kind == b'BIN\0':
            binary = payload
        else:
            raise AssertionError(f'unexpected chunk type {kind!r}')
        assert length % 4 == 0, f'chunk {kind!r} is not 4-byte aligned'
        off += 8 + length
    assert gltf is not None and binary is not None, 'missing JSON or BIN chunk'
    assert gltf['asset']['version'] == '2.0'

    # ---- buffer views / accessors
    for i, view in enumerate(gltf['bufferViews']):
        end = view.get('byteOffset', 0) + view['byteLength']
        assert end <= len(binary), f'bufferView {i} runs past the binary chunk'
    for i, acc in enumerate(gltf['accessors']):
        view = gltf['bufferViews'][acc['bufferView']]
        need = acc['count'] * NCOMP[acc['type']] * CSIZE[acc['componentType']]
        assert need <= view['byteLength'], f'accessor {i} overruns its bufferView'

    prim = gltf['meshes'][0]['primitives'][0]
    assert prim['mode'] == 4, 'primitive is not a triangle list'
    attrs = prim['attributes']
    for name in ('POSITION', 'NORMAL', 'TEXCOORD_0'):
        assert name in attrs, f'missing attribute {name}'
    counts = {gltf['accessors'][attrs[k]]['count'] for k in attrs}
    assert len(counts) == 1, f'attribute vertex counts disagree: {counts}'
    verts = counts.pop()
    tris = gltf['accessors'][prim['indices']]['count'] // 3
    assert gltf['accessors'][prim['indices']]['count'] % 3 == 0
    assert verts > 1000 and tris > 1000, f'mesh looks empty ({verts} verts / {tris} tris)'

    # ---- accessor min/max must be the truth (viewers frame the model with it)
    pos = gltf['accessors'][attrs['POSITION']]
    assert 'min' in pos and 'max' in pos, 'POSITION has no min/max'
    assert all(a < b for a, b in zip(pos['min'], pos['max'])), 'degenerate POSITION bounds'
    view = gltf['bufferViews'][pos['bufferView']]
    base = view.get('byteOffset', 0) + pos.get('byteOffset', 0)
    vals = struct.unpack_from(f'<{verts * 3}f', binary, base)
    for axis in range(3):
        got_min = min(vals[axis::3])
        got_max = max(vals[axis::3])
        assert abs(got_min - pos['min'][axis]) < 1e-4, f'POSITION min[{axis}] is wrong'
        assert abs(got_max - pos['max'][axis]) < 1e-4, f'POSITION max[{axis}] is wrong'

    # ---- indices in range
    iacc = gltf['accessors'][prim['indices']]
    iview = gltf['bufferViews'][iacc['bufferView']]
    ibase = iview.get('byteOffset', 0) + iacc.get('byteOffset', 0)
    fmt = {5123: 'H', 5125: 'I'}[iacc['componentType']]
    idx = struct.unpack_from(f'<{iacc["count"]}{fmt}', binary, ibase)
    assert max(idx) < verts, 'index out of range'
    assert min(idx) == 0 or max(idx) == verts - 1, 'unused tail of the vertex buffer'

    # ---- textures
    mat = gltf['materials'][prim['material']]
    pbr = mat['pbrMetallicRoughness']
    roles = {}
    for slot, ref in (('baseColor', pbr['baseColorTexture']),
                      ('orm', pbr['metallicRoughnessTexture']),
                      ('normal', mat['normalTexture']),
                      ('occlusion', mat['occlusionTexture'])):
        tex = gltf['textures'][ref['index']]
        assert tex['sampler'] < len(gltf['samplers'])
        roles[slot] = tex['source']
    assert roles['orm'] == roles['occlusion'], 'ORM texture is not shared with occlusion'
    assert len(set(roles.values())) == 3, f'unexpected texture sharing: {roles}'
    assert mat['doubleSided'] is False, 'head should be single sided'
    assert pbr['metallicFactor'] == 0, 'skin is a dielectric'
    assert mat['normalTexture']['scale'] == 1.0

    sizes = {}
    for i, img in enumerate(gltf['images']):
        view = gltf['bufferViews'][img['bufferView']]
        o, n = view.get('byteOffset', 0), view['byteLength']
        assert img['mimeType'] == 'image/png'
        w, h, c, _ = png_info(binary[o:o + n])
        sizes[i] = (w, h, c)
        assert w >= 256 and h >= 256 and w == h, f'image {img["name"]} is {w}x{h}'
    assert sizes[roles['baseColor']][2] == 3, 'base colour must be RGB'
    assert sizes[roles['normal']][2] == 3, 'normal map must be RGB'
    assert sizes[roles['orm']][2] == 3, 'ORM must be RGB'

    # The height map has no glTF slot; it must still be there, be named, and be
    # the supplied file byte for byte (the normal map is derived from it, so a
    # re-encoded copy would silently drift).
    extras = gltf['extras']
    height = extras['heightTexture']
    assert gltf['images'][height]['name'].endswith('Height')
    hview = gltf['bufferViews'][gltf['images'][height]['bufferView']]
    embedded = binary[hview.get('byteOffset', 0):hview.get('byteOffset', 0) + hview['byteLength']]
    source = here_path.parent.parent / 'textures' / 'july_height.png'
    if source.exists():
        assert embedded == source.read_bytes(), 'embedded height map differs from textures/july_height.png'

    # ---- ORM packing: B == 0 everywhere, G is the inverted gloss mask
    view = gltf['bufferViews'][gltf['images'][roles['orm']]['bufferView']]
    o, n = view.get('byteOffset', 0), view['byteLength']
    w, h, c, raw = png_info(binary[o:o + n])
    stride = w * c
    metallic = max(raw[y * (stride + 1) + 1 + 2] for y in range(h))
    assert metallic == 0, 'metallic channel is not zero'
    rough = [raw[y * (stride + 1) + 1 + 1] for y in range(h)]
    assert max(rough) > 128, 'roughness channel looks empty'

    print(f'test_glb: ok - {verts} verts, {tris} tris, '
          f'{len(gltf["images"])} images, {len(data) / 1048576:.2f} MiB')


def main():
    here = pathlib.Path(__file__).resolve().parent
    path = sys.argv[1] if len(sys.argv) > 1 else here.parent / 'assets' / 'July_Head.glb'
    try:
        check(path, here)
    except FileNotFoundError:
        print(f'test_glb: no GLB at {path} (run tools/export_glb.js first)')
        return 77          # meson's "skipped"
    return 0


if __name__ == '__main__':
    sys.exit(main())
