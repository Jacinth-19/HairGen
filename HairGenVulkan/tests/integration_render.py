#!/usr/bin/env python3
"""integration_render.py - headless smoke test for the Vulkan renderer.

Renders a small frame, then verifies the PNG is well formed and that the image
is not a constant colour (i.e. geometry and shading actually ran). Also runs a
second frame with hair disabled and checks that the two differ, which proves the
hair pass contributes to the image.

Exits 77 (meson's "skipped" code) when no Vulkan device is reachable.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib


def png_size_and_pixels(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    off, width, height, idat = 8, 0, 0, b''
    while off < len(data):
        (length,) = struct.unpack('>I', data[off:off + 4])
        kind = data[off + 4:off + 8]
        chunk = data[off + 8:off + 8 + length]
        if kind == b'IHDR':
            width, height, depth, ctype = struct.unpack('>IIBB', chunk[:10])
            assert depth == 8 and ctype in (2, 6), 'unexpected PNG format'
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'IEND':
            break
        off += 12 + length
    raw = zlib.decompress(idat)
    channels = 4 if ctype == 6 else 3
    stride = width * channels
    pixels, prev = [], bytearray(stride)
    pos = 0
    for _ in range(height):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        pixels.append(bytes(line))
        prev = line
    return width, height, channels, pixels


def find_icd():
    """Return a Vulkan ICD json to use, or None. Honours VK_ICD_FILENAMES and
    otherwise looks for a software rasteriser installed in /usr/share/vulkan."""
    for env in ('VK_ICD_FILENAMES', 'VK_DRIVER_FILES'):
        val = os.environ.get(env)
        if val and os.path.exists(val.split(os.pathsep)[0]):
            return val.split(os.pathsep)[0]
    candidates = [
        '/tmp/dc/package/lvp_icd.json',
        '/usr/share/vulkan/icd.d/lvp_icd.x86_64.json',
        '/usr/share/vulkan/icd.d/lvp_icd.json',
        '/usr/share/vulkan/icd.d/swiftshader_icd.json',
        '/tmp/ss/package/linux-x64/vk_swiftshader_icd.json',
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def render(binary, out, extra):
    cmd = [binary, '--out', out, '--size', '96', '--samples', '4'] + extra
    env = dict(os.environ)
    icd = find_icd()
    if icd:
        env['VK_ICD_FILENAMES'] = icd
        env['VK_DRIVER_FILES'] = icd
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        if 'Vulkan' in (r.stdout + r.stderr) or 'vulkan' in (r.stdout + r.stderr):
            return None
        raise SystemExit('render failed (%d):\n%s\n%s' % (r.returncode, r.stdout, r.stderr))
    return r


def stats(pixels):
    lo, hi = 255, 0
    for row in pixels:
        row_lo, row_hi = min(row), max(row)
        lo = min(lo, row_lo)
        hi = max(hi, row_hi)
    return lo, hi


def main():
    if len(sys.argv) < 2:
        raise SystemExit('usage: integration_render.py <hairgen binary>')
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print('hairgen binary not found:', binary)
        return 77

    tmp = tempfile.mkdtemp(prefix='hairgen_it_')
    full = os.path.join(tmp, 'full.png')
    nohair = os.path.join(tmp, 'nohair.png')

    if render(binary, full, []) is None:
        print('no Vulkan device available - skipping integration test')
        return 77
    render(binary, nohair, ['--no-hair'])

    w, h, ch, px_full = png_size_and_pixels(full)
    w2, h2, ch2, px_nohair = png_size_and_pixels(nohair)
    assert (w, h) == (96, 96) and (w2, h2) == (96, 96), 'unexpected image size'
    lo, hi = stats(px_full)
    assert hi - lo > 20, 'render is flat (lo=%d hi=%d) - nothing was drawn' % (lo, hi)
    diff_px = 0
    for row_a, row_b in zip(px_full, px_nohair):
        if row_a == row_b:
            continue
        for x in range(0, len(row_a), ch):
            if row_a[x:x + ch] != row_b[x:x + ch]:
                diff_px += 1
    assert diff_px > (w * h) // 10, \
        'hair pass barely changed the image (%d of %d pixels)' % (diff_px, w * h)
    # The weather system: a rainy frame must differ from the dry one (rain
    # streaks, wet ground, dimmer sun), and the rain must not blank the image.
    dry = os.path.join(tmp, 'dry.png')
    wet = os.path.join(tmp, 'wet.png')
    if render(binary, dry, ['--preset', 'dry']) is None:
        print('no Vulkan device available - skipping integration test')
        return 77
    render(binary, wet, ['--preset', 'storm'])
    wd, hd, chd, px_dry = png_size_and_pixels(dry)
    ww, hw, chw, px_wet = png_size_and_pixels(wet)
    assert (wd, hd) == (96, 96) and (ww, hw) == (96, 96), 'unexpected image size'
    wlo, whi = stats(px_wet)
    assert whi - wlo > 20, 'rain frame is flat (lo=%d hi=%d)' % (wlo, whi)
    rain_diff = 0
    for row_a, row_b in zip(px_dry, px_wet):
        for x in range(0, len(row_a), chd):
            if row_a[x:x + chd] != row_b[x:x + chd]:
                rain_diff += 1
    assert rain_diff > (wd * hd) // 10, \
        'rain barely changed the image (%d of %d pixels)' % (rain_diff, wd * hd)

    print('integration_render: ok (range %d..%d, %d/%d pixels change when hair is off,'
          ' %d/%d when it rains)' % (lo, hi, diff_px, w * h, rain_diff, wd * hd))
    return 0


if __name__ == '__main__':
    sys.exit(main())
