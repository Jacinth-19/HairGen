#!/usr/bin/env python3
"""preview_glb.py - independent check that a GLB of the head is self-consistent.

Rasterises the GLB with nothing but numpy + Pillow (no glTF library, no GPU), which
is a stronger statement than "the file parses": geometry, UVs and the embedded
base-colour texture all have to agree for the face to land on the mesh the right
way round. Used to verify assets/July_Head.glb when it is regenerated.

    python3 tools/preview_glb.py assets/July_Head.glb media/July_Head_preview.png
"""
import io
import json
import pathlib
import struct
import sys

import numpy as np
from PIL import Image

COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32,
        5126: np.float32}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def load_glb(path):
    data = open(path, 'rb').read()
    magic, version, total = struct.unpack('<4sII', data[:12])
    assert magic == b'glTF' and version == 2 and total == len(data), 'not a GLB 2.0'
    off, gltf, binary = 12, None, None
    while off < len(data):
        length, kind = struct.unpack('<I4s', data[off:off + 8])
        payload = data[off + 8:off + 8 + length]
        if kind == b'JSON':
            gltf = json.loads(payload)
        elif kind == b'BIN\0':
            binary = payload
        off += 8 + length + ((4 - length % 4) % 4 if length % 4 else 0)
    return gltf, binary


def accessor(gltf, binary, index):
    acc = gltf['accessors'][index]
    view = gltf['bufferViews'][acc['bufferView']]
    dtype = np.dtype(COMP[acc['componentType']])
    n = NCOMP[acc['type']]
    start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    count = acc['count']
    stride = view.get('byteStride') or dtype.itemsize * n
    if stride == dtype.itemsize * n:
        arr = np.frombuffer(binary, dtype=dtype, count=count * n, offset=start)
        return arr.reshape(count, n)
    out = np.empty((count, n), dtype=dtype)
    for i in range(count):
        out[i] = np.frombuffer(binary, dtype=dtype, count=n, offset=start + i * stride)
    return out


def image_rgb(gltf, binary, index):
    img = gltf['images'][index]
    view = gltf['bufferViews'][img['bufferView']]
    o, n = view.get('byteOffset', 0), view['byteLength']
    im = Image.open(io.BytesIO(binary[o:o + n])).convert('RGB')
    return np.asarray(im, dtype=np.float32) / 255.0


def render(gltf, binary, yaw, size=420, fov=36.0):
    prim = gltf['meshes'][0]['primitives'][0]
    pos = accessor(gltf, binary, prim['attributes']['POSITION']).astype(np.float32)
    nrm = accessor(gltf, binary, prim['attributes']['NORMAL']).astype(np.float32)
    uv = accessor(gltf, binary, prim['attributes']['TEXCOORD_0']).astype(np.float32)
    tri = accessor(gltf, binary, prim['indices']).reshape(-1, 3).astype(np.int64)

    mat = gltf['materials'][prim['material']]
    base = image_rgb(gltf, binary, gltf['textures'][mat['pbrMetallicRoughness']
                                                    ['baseColorTexture']['index']]['source'])
    ao_tex = image_rgb(gltf, binary, gltf['textures'][mat['occlusionTexture']['index']]['source'])[:, :, 0]

    lo, hi = pos.min(axis=0), pos.max(axis=0)
    centre = 0.5 * (lo + hi)
    # Frame on the vertical extent (the head is the tallest thing here), not on
    # the bounding diagonal, which leaves the model lost in the frame.
    dist = float(hi[1] - lo[1]) * 0.5 / np.tan(np.radians(fov) * 0.5) * 1.15

    c, s = np.cos(yaw), np.sin(yaw)
    rot_y = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float32)
    # light comes from the camera's upper left, in view space
    light = np.array([-0.45, 0.55, 0.70], dtype=np.float32)
    light /= np.linalg.norm(light)

    view = (pos - centre) @ rot_y.T
    vnrm = nrm @ rot_y.T
    depth = view[:, 2] + dist
    f = 1.0 / np.tan(np.radians(fov) * 0.5)
    sx = view[:, 0] * f / depth * 0.5 + 0.5
    sy = 0.5 - view[:, 1] * f / depth * 0.5
    px, py = sx * size, sy * size

    colour = np.zeros((size, size, 3), dtype=np.float32) + 0.09
    zbuf = np.full((size, size), np.inf, dtype=np.float32)

    for a, b, cc in tri:
        x0, y0, z0 = px[a], py[a], depth[a]
        x1, y1, z1 = px[b], py[b], depth[b]
        x2, y2, z2 = px[cc], py[cc], depth[cc]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue
        xmin, xmax = int(max(0, np.floor(min(x0, x1, x2)))), int(min(size - 1, np.ceil(max(x0, x1, x2))))
        ymin, ymax = int(max(0, np.floor(min(y0, y1, y2)))), int(min(size - 1, np.ceil(max(y0, y1, y2))))
        if xmin > xmax or ymin > ymax:
            continue
        gx, gy = np.meshgrid(np.arange(xmin, xmax + 1) + 0.5, np.arange(ymin, ymax + 1) + 0.5)
        w0 = ((x1 - gx) * (y2 - gy) - (x2 - gx) * (y1 - gy)) / area
        w1 = ((x2 - gx) * (y0 - gy) - (x0 - gx) * (y2 - gy)) / area
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        z = w0 * z0 + w1 * z1 + w2 * z2
        sub = zbuf[ymin:ymax + 1, xmin:xmax + 1]
        hit = inside & (z < sub)
        if not hit.any():
            continue

        n = (w0[..., None] * vnrm[a] + w1[..., None] * vnrm[b] + w2[..., None] * vnrm[cc])
        n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
        lam = np.maximum(n @ light, 0.0)

        tuv = (w0[..., None] * uv[a] + w1[..., None] * uv[b] + w2[..., None] * uv[cc])
        # glTF: v = 0 is the first row of the image
        tx = np.clip((tuv[..., 0] * (base.shape[1] - 1)).astype(np.int32), 0, base.shape[1] - 1)
        ty = np.clip((tuv[..., 1] * (base.shape[0] - 1)).astype(np.int32), 0, base.shape[0] - 1)
        albedo = base[ty, tx]
        ao = 1.0 if ao_tex is None else \
            1.0 - (1.0 - ao_tex[ty, tx]) * 0.75

        shade = albedo * ((lam[..., None] * 0.85 + 0.35) * ao[..., None])
        sub[hit] = z[hit]
        colour[ymin:ymax + 1, xmin:xmax + 1][hit] = shade[hit]

    return np.clip(colour, 0, 1) ** (1 / 2.2)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'assets/July_Head.glb'
    dst = sys.argv[2] if len(sys.argv) > 2 else None
    gltf, binary = load_glb(src)
    names = ['front', 'right', 'back', 'left']
    tiles = [render(gltf, binary, np.radians(a)) for a in (0, 90, 180, 270)]
    sheet = np.concatenate(tiles, axis=1)
    img = Image.fromarray((sheet * 255).astype(np.uint8))
    from PIL import ImageDraw, ImageFont
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(str(pathlib.Path(__file__).parent.parent /
                                      'assets/fonts/Roboto-Regular.ttf'), 15)
    except OSError:
        font = ImageFont.load_default()
    for i, name in enumerate(names):
        draw.text((i * tiles[0].shape[1] + 10, 8), name, fill=(235, 235, 235), font=font)
    if dst:
        img.save(dst)
        print(f'preview_glb: wrote {dst} ({img.width}x{img.height})')
    else:
        img.show()


if __name__ == '__main__':
    main()
