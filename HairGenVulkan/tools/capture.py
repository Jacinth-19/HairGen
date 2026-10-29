#!/usr/bin/env python3
"""capture.py - renders the deliverable media (stills, animation, GIF, video).

  python3 tools/capture.py --binary build/hairgen [--out media] [--size 640]
                           [--frames 48] [--fps 24] [--only stills|anim|sheet]

Everything goes through the `hairgen` CLI, so it works with any Vulkan device
(SwiftShader / lavapipe / a real GPU). Video is written twice: an animated GIF
and an MJPEG AVI (no ffmpeg needed - the container is assembled here), plus an
animated WebP when ImageMagick supports it.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

STYLES = {
    'blond': ['--style', 'blond'],
    'brown': ['--style', 'brown'],
    'rosa': ['--style', 'rosa'],
    'curly': ['--style', 'curly'],
    'straight': ['--style', 'straight'],
}

# ---------------------------------------------------------------- helpers


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write('command failed: %s\n%s\n%s\n' % (' '.join(cmd), r.stdout, r.stderr))
        raise SystemExit(1)
    return r


def have(tool):
    return shutil.which(tool) is not None


def render(binary, out, size, extra):
    run([binary, '--out', out, '--size', str(size)] + extra)


def montage(paths, labels, out, tile=None):
    """Contact sheet with labels, using ImageMagick."""
    cmd = ['montage']
    for p in paths:
        cmd.append(p)
    cmd += ['-label', '%s', '-tile', tile or ('%dx' % len(paths)), '-geometry', '+6+6',
            '-background', '#1b1b1b', '-fill', '#cfcfcf', '-pointsize', '18',
            '-font', 'DejaVu-Sans', out]
    # `-label` before each image needs a per-image label; do it explicitly.
    cmd = ['montage']
    for p, lab in zip(paths, labels):
        cmd += ['-label', lab, p]
    cmd += ['-tile', tile or ('%dx' % len(paths)), '-geometry', '+6+6',
            '-background', '#1b1b1b', '-fill', '#cfcfcf', '-pointsize', '18',
            '-font', 'DejaVu-Sans', out]
    run(cmd)


# ---------------------------------------------------------------- AVI (MJPEG)


def write_mjpeg_avi(path, frame_paths, fps):
    """Assemble an MJPEG AVI from JPEG frames - a real, seekable video file."""
    frames = []
    for p in frame_paths:
        with open(p, 'rb') as f:
            frames.append(f.read())
    if not frames:
        return False
    # Dimensions come from the first JPEG's SOF marker.
    width = height = 0
    j = frames[0]
    i = 2
    while i < len(j) - 9:
        if j[i] != 0xFF:
            i += 1
            continue
        marker = j[i + 1]
        if marker in (0xC0, 0xC1, 0xC2):
            height, width = struct.unpack('>HH', j[i + 5:i + 9])
            break
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        (seg,) = struct.unpack('>H', j[i + 2:i + 4])
        i += 2 + seg
    if not width or not height:
        raise SystemExit('could not parse JPEG dimensions for AVI')

    max_frame = max(len(f) for f in frames)
    movi_data = bytearray()
    index = bytearray()
    for f in frames:
        off = len(movi_data) + 4  # offsets are relative to the 'movi' data start
        movi_data += b'00dc' + struct.pack('<I', len(f)) + f
        if len(f) % 2:
            movi_data += b'\x00'
        index += b'00dc' + struct.pack('<III', 0x10, off, len(f))

    avih = struct.pack('<IIIIIIIIII4I',
                       int(round(1e6 / float(fps))), 0, 0, 0x10, len(frames), 0, 1,
                       max_frame, width, height, 0, 0, 0, 0)
    strh = (b'vids' + b'MJPG' + struct.pack('<IHHIIIIIIIIhhhh',
                                            0, 0, 0, 0, 1, fps, 0, len(frames),
                                            max_frame, 0xFFFFFFFF, 0,
                                            0, 0, width, height))
    strf = struct.pack('<IiiHH4sIiiII', 40, width, height, 1, 24, b'MJPG', 0,
                       0, 0, 0, 0)

    def chunk(cid, data):
        out = cid + struct.pack('<I', len(data)) + data
        if len(data) % 2:
            out += b'\x00'
        return out

    def list_chunk(kind, data):
        return chunk(b'LIST', kind + data)

    strl = list_chunk(b'strl', chunk(b'strh', strh) + chunk(b'strf', strf))
    hdrl = list_chunk(b'hdrl', chunk(b'avih', avih) + strl)
    movi = list_chunk(b'movi', bytes(movi_data))
    body = hdrl + movi + chunk(b'idx1', bytes(index))
    riff = b'RIFF' + struct.pack('<I', len(body) + 4) + b'AVI ' + body
    with open(path, 'wb') as f:
        f.write(riff)
    return True


# ---------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', default=os.path.join(ROOT, 'build', 'hairgen'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'media'))
    ap.add_argument('--size', type=int, default=640)
    ap.add_argument('--anim-size', type=int, default=512)
    ap.add_argument('--frames', type=int, default=48)
    ap.add_argument('--fps', type=int, default=24)
    ap.add_argument('--only', default=None, help='stills | anim | sheet')
    ap.add_argument('--assets', default=os.path.join(ROOT, 'assets'))
    args = ap.parse_args()

    binary = os.path.abspath(args.binary)
    if not os.path.exists(binary):
        raise SystemExit('hairgen binary not found: ' + binary)
    out = os.path.abspath(args.out)
    os.makedirs(os.path.join(out, 'stills'), exist_ok=True)
    os.makedirs(os.path.join(out, 'frames'), exist_ok=True)
    os.chdir(os.path.dirname(binary))  # the binary looks for assets/ relative to cwd
    os.symlink(args.assets, 'assets') if not os.path.exists('assets') else None

    size = args.size
    stills = []

    if args.only in (None, 'stills'):
        hero = os.path.join(out, 'stills', 'hero.png')
        print('[capture] hero still')
        render(binary, hero, size, ['--samples', '4', '--sun-intensity', '1.0'])
        stills.append((hero, 'default (blond bob)'))

        for name, extra in STYLES.items():
            p = os.path.join(out, 'stills', 'style_%s.png' % name)
            print('[capture] style', name)
            render(binary, p, size, ['--samples', '4'] + extra)
            stills.append((p, 'style: ' + name))

        # Lighting / post-processing sweep.
        sweep = [
            ('light_morning', ['--sun-rot', '2.6', '--sun-tilt', '-0.25', '--exposure', '1.15']),
            ('light_noon', ['--sun-rot', '1.4', '--sun-tilt', '-1.25', '--exposure', '0.85']),
            ('light_backlit', ['--sun-rot', '5.3', '--sun-tilt', '-0.35', '--exposure', '1.3',
                               '--bloom', '0.5']),
            ('light_studio', ['--sun-rot', '2.0', '--sun-tilt', '-0.9', '--clouds', '0.0',
                              '--bloom', '0.15']),
        ]
        for name, extra in sweep:
            p = os.path.join(out, 'stills', name + '.png')
            print('[capture]', name)
            render(binary, p, size, ['--samples', '4'] + extra)
            stills.append((p, name))

        # Hair control sweep (each one changes a single group).
        controls = [
            ('fuzz', ['--fuzz', '0.45']),
            ('frizz', ['--frizz', '1.4']),
            ('kink', ['--kink', '0.35']),
            ('clump', ['--clump', '0.35', '--clump-size', '0.12']),
            ('curl', ['--curl', '0.9', '--curl-twists', '14']),
            ('shadows', ['--density', '6.0']),
        ]
        for name, extra in controls:
            p = os.path.join(out, 'stills', 'control_%s.png' % name)
            print('[capture] control', name)
            render(binary, p, size, ['--samples', '4'] + extra)
            stills.append((p, 'control: ' + name))

        # Demo-fidelity modes ported from the reference source.
        modes = [
            ('mode_lines', ['--lines']),
            ('mode_ribbons', ['--render-mode', '1']),
            ('mode_root_samples', ['--root-samples']),
            ('mode_hairmesh', ['--show-hairmesh', '--hair-count', '6000']),
            ('mode_no_collide', ['--no-collide']),
        ]
        for name, extra in modes:
            p = os.path.join(out, 'stills', name + '.png')
            print('[capture]', name)
            render(binary, p, size, ['--samples', '4'] + extra)
            stills.append((p, name))

        # A high resolution hero for print / README.
        render(binary, os.path.join(out, 'hero_1536.png'), 1536, ['--samples', '4'])
        # Nice-to-have: anti-aliasing comparison (1x vs 4x MSAA crop).
        render(binary, os.path.join(out, 'aa_off.png'), size, ['--samples', '1'])
        render(binary, os.path.join(out, 'aa_on.png'), size, ['--samples', '4'])

    if args.only in (None, 'sheet'):
        if not stills:
            stills = [(p, os.path.splitext(os.path.basename(p))[0])
                      for p in sorted(
                          os.path.join(out, 'stills', f)
                          for f in os.listdir(os.path.join(out, 'stills')))[:12]]
        if have('montage'):
            print('[capture] contact sheet')
            montage([p for p, _ in stills[:12]], [l for _, l in stills[:12]],
                    os.path.join(out, 'contact_sheet.png'), tile='4x')
            # A JPEG copy keeps the sheet small enough to embed in a README.
            run(['convert', os.path.join(out, 'contact_sheet.png'),
                 '-resize', '1800x', '-quality', '92', '-strip',
                 os.path.join(out, 'contact_sheet.jpg')])
            os.remove(os.path.join(out, 'contact_sheet.png'))

    if args.only in (None, 'anim'):
        n = args.frames
        asz = args.anim_size
        seq = os.path.join(out, 'frames')
        print('[capture] orbit animation: %d frames @ %d px' % (n, asz))
        run([binary, '--sequence', seq, '--frames', str(n), '--fps', str(args.fps),
             '--anim', 'sweep', '--size', str(asz), '--samples', '2'])
        pattern = [os.path.join(seq, 'frame_%04d.png' % i) for i in range(n)]
        pattern = [p for p in pattern if os.path.exists(p)]
        if have('convert') and pattern:
            delay = max(2, int(round(100.0 / args.fps)))
            print('[capture] GIF + WebP')
            gif = os.path.join(out, 'orbit.gif')
            # Build the GIF with a shared palette and one optimised delta frame
            # per image, so the file stays small enough to embed in a README.
            run(['convert', '-delay', str(delay), '-loop', '0'] + pattern +
                ['-colors', '256', '-layers', 'Optimize', '-dither', 'FloydSteinberg', gif])
            try:
                run(['convert', '-delay', str(delay), '-loop', '0'] + pattern +
                    ['-colors', '256', '-quality', '82', '-define',
                     'webp:method=4', os.path.join(out, 'orbit.webp')])
            except SystemExit:
                pass
            # MJPEG frames -> AVI
            jpgs = []
            for i, p in enumerate(pattern):
                j = os.path.join(seq, 'frame_%04d.jpg' % i)
                run(['convert', p, '-quality', '92', j])
                jpgs.append(j)
            print('[capture] AVI (MJPEG)')
            write_mjpeg_avi(os.path.join(out, 'orbit.avi'), jpgs, args.fps)
            # A poster strip of the animation.
            picks = pattern[::max(1, len(pattern) // 6)][:6]
            if have('montage'):
                montage(picks, ['t=%d/%d' % (i, len(pattern)) for i in
                                range(0, len(pattern), max(1, len(pattern) // 6))][:len(picks)],
                        os.path.join(out, 'animation_strip.png'), tile='6x')

    print('[capture] done -', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
