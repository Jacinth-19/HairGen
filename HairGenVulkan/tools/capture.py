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

# Hair dye swatches: the renderer ships rosa by default and the dye layer can
# recolour the fibres to anything, so the sheet walks the palette.
STYLES = {
    'rosa': ['--dye', 'rosa'],
    'blonde': ['--dye', 'blonde', '--dye-strength', '0.45'],
    'copper': ['--dye', 'copper'],
    'auburn': ['--dye', 'auburn'],
    'burgundy': ['--dye', 'burgundy'],
    'violet': ['--dye', 'violet'],
    'teal': ['--dye', 'teal'],
    'platinum': ['--dye', 'platinum'],
    # Two-tone: an ombre from a dark root to a pale tip, and a harder dip dye.
    'ombre': ['--dye', 'chestnut', '--dye-tip', 'honey', '--dye-gradient', '0.8'],
    'dipdye': ['--dye', 'jet', '--dye-tip', 'rosa', '--dye-gradient', '1.6'],
    'balayage': ['--dye', 'espresso', '--dye-tip', 'blonde', '--dye-gradient', '1.0'],
}

# Procedural hairstyles (one guide mesh, restyled by the operator stack).
# Eight per sheet: the long/female set, then the short/male set.
HAIRSTYLES = ['bob', 'lob', 'sleek', 'long', 'layered', 'wavy', 'curls', 'coils',
              'afro', 'shag', 'pixie', 'fringe', 'volume', 'editorial', 'wet_look', 'punk',
              'buzz', 'crew', 'fade', 'textured', 'side_part', 'quiff', 'pompadour',
              'slick_back', 'curly_top', 'mohawk']

# Weather presets: the Preetham atmosphere, the rain system, the wet ground and
# the lens droplets, one still each.
WEATHER = [
    ('dry', ['--preset', 'dry']),
    ('drizzle', ['--preset', 'drizzle']),
    ('rain', ['--preset', 'rain']),
    ('storm', ['--preset', 'storm']),
    ('sunset', ['--preset', 'sunset']),
    ('noon', ['--preset', 'noon', '--clouds', '0.35']),
    ('puddles', ['--preset', 'rain', '--puddles', '0.85', '--wind', '0.9']),
    ('wet-closeup', ['--preset', 'rain', '--dist', '1.9', '--exposure', '0.85']),
]

# Studio floor: the checkerboard is a perception aid (it gives the eye a scale
# reference and makes the contact shadow read), so the sheet shows it on, off
# and in its two colour schemes.
GROUND = [
    ('checker', ['--checker', '1.0']),
    ('checker-off', ['--no-checker']),
    ('checker-white', ['--checker', '1.0', '--checker-scale', '0.8']),
    ('checker-fine', ['--checker', '1.0', '--checker-scale', '4.0', '--checker-fade', '4.0']),
]

# Lighting looks, straight from the panel's presets.
LOOKS = [
    ('studio', ['--rot', '3.05', '--sun-rot', '2.60', '--sun-tilt', '-0.55']),
    ('golden', ['--preset', 'sunset']),
    ('backlit', ['--sun-rot', '5.35', '--sun-tilt', '-0.30', '--bloom', '0.55', '--exposure', '0.85']),
    ('overcast', ['--clouds', '0.85', '--turbidity', '4.5', '--sun-intensity', '0.7']),
]

# ---------------------------------------------------------------- helpers


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write('command failed: %s\n%s\n%s\n' % (' '.join(cmd), r.stdout, r.stderr))
        raise SystemExit(1)
    return r


def have(tool):
    return shutil.which(tool) is not None


# Quality knobs shared by every still: 4x MSAA plus a 2x supersample, and a
# 2048^2 shadow map (the hair shadow lands on the face, so resolution shows).
QUALITY = ['--samples', '4', '--ssaa', '2', '--shadow-res', '2048']


def render(binary, out, size, extra):
    run([binary, '--out', out, '--size', str(size)] + QUALITY + extra)


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
        render(binary, hero, size, ['--sun-intensity', '1.0'])
        stills.append((hero, 'default: rosa dye, bob'))

        # Dye palette.
        for name, extra in STYLES.items():
            p = os.path.join(out, 'stills', 'dye_%s.png' % name)
            print('[capture] dye', name)
            render(binary, p, size, extra)
            stills.append((p, 'dye: ' + name))

        # Hairstyle presets.
        for name in HAIRSTYLES:
            p = os.path.join(out, 'stills', 'hairstyle_%s.png' % name)
            print('[capture] hairstyle', name)
            render(binary, p, size, ['--style', name])
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
            render(binary, p, size, extra)
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
            render(binary, p, size, extra)
            stills.append((p, 'control: ' + name))

        # Studio floor: the analytic checkerboard, on/off and at two scales.
        for name, extra in GROUND:
            p = os.path.join(out, 'stills', 'ground_%s.png' % name)
            print('[capture] ground', name)
            render(binary, p, size, extra)
            stills.append((p, 'ground: ' + name))

        # Lighting looks (the same presets the panel's Look menu offers).
        for name, extra in LOOKS:
            p = os.path.join(out, 'stills', 'look_%s.png' % name)
            print('[capture] look', name)
            render(binary, p, size, extra)
            stills.append((p, 'look: ' + name))

        # Weather / liquid effects.
        for name, extra in WEATHER:
            p = os.path.join(out, 'stills', 'weather_%s.png' % name)
            print('[capture] weather', name)
            render(binary, p, size, extra)
            stills.append((p, 'weather: ' + name))

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
            render(binary, p, size, extra)
            stills.append((p, name))

        # The dockable ImGui panel. --ui-dump renders the UI on the CPU over a
        # stand-in background, so these screenshots need no Vulkan device at all.
        panel_pngs = []
        full = os.path.join(out, 'stills', 'panel.png')
        try:
            run([binary, '--ui-dump', full, '--size', str(size)])
            panel_pngs.append((full, 'panel'))
        except SystemExit:
            print('[capture] panel dump failed (fonts missing?)')
        for win in ('hair', 'colour', 'styles', 'lighting', 'ground', 'weather', 'camera', 'output'):
            p = os.path.join(out, 'stills', 'panel_%s.png' % win)
            env = dict(os.environ, HGEN_UI_FOCUS=win)
            try:
                run([binary, '--ui-dump', p, '--size', str(size)], env=env)
                panel_pngs.append((p, 'panel: ' + win))
            except SystemExit:
                pass

        # A high resolution hero for print / README.
        run([binary, '--out', os.path.join(out, 'hero_1536.png'), '--size', '1536', '--samples', '4',
             '--shadow-res', '2048'])
        # Nice-to-have: anti-aliasing comparison (1x vs 4x MSAA crop).
        run([binary, '--out', os.path.join(out, 'aa_off.png'), '--size', str(size), '--samples', '1',
             '--ssaa', '1'])
        run([binary, '--out', os.path.join(out, 'aa_on.png'), '--size', str(size), '--samples', '4',
             '--ssaa', '2'])

        # Dye palette sheet (all swatches at a glance).
        dye_paths = [os.path.join(out, 'stills', 'dye_%s.png' % n) for n in STYLES]
        dye_paths = [p for p in dye_paths if os.path.exists(p)]
        if have('montage') and dye_paths:
            print('[capture] dye sheet')
            tmp = os.path.join(out, 'dye_sheet.png')
            montage(dye_paths, list(STYLES.keys()), tmp, tile='4x')
            run(['convert', tmp, '-resize', '1600x', '-quality', '92', '-strip',
                 os.path.join(out, 'dye_sheet.jpg')])
            os.remove(tmp)

        # Weather sheet (rain, puddles, lens droplets).
        wx_paths = [os.path.join(out, 'stills', 'weather_%s.png' % n) for n, _ in WEATHER]
        wx_paths = [p for p in wx_paths if os.path.exists(p)]
        if have('montage') and wx_paths:
            print('[capture] weather sheet')
            tmp = os.path.join(out, 'weather_sheet.png')
            montage(wx_paths, [n for n, _ in WEATHER], tmp, tile='4x')
            run(['convert', tmp, '-resize', '1600x', '-quality', '92', '-strip',
                 os.path.join(out, 'weather_sheet.jpg')])
            os.remove(tmp)

        # Hairstyle sheet.
        hair_paths = [os.path.join(out, 'stills', 'hairstyle_%s.png' % n) for n in HAIRSTYLES]
        hair_paths = [p for p in hair_paths if os.path.exists(p)]
        if have('montage') and hair_paths:
            print('[capture] hairstyle sheet')
            tmp = os.path.join(out, 'hairstyle_sheet.png')
            montage(hair_paths, HAIRSTYLES, tmp, tile='4x')
            run(['convert', tmp, '-resize', '1600x', '-quality', '92', '-strip',
                 os.path.join(out, 'hairstyle_sheet.jpg')])
            os.remove(tmp)

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
            # One palette shared by every frame, no per-frame delta encoding.
            # `-layers Optimize` writes frames that differ from their neighbour
            # only in the transparent index, and ImageMagick's optimiser then
            # drops scattered single pixels to the *previous* frame's colour.
            # Over a smooth sky gradient that shows up as a ragged white blob
            # that is not in the rendered PNGs at all. The plain encoding is
            # within 5% of the optimised size.
            run(['convert', '-delay', str(delay), '-loop', '0'] + pattern +
                ['-colors', '256', gif])
            try:
                run(['convert', '-delay', str(delay), '-loop', '0'] + pattern +
                    ['-colors', '256', '-quality', '88', '-define',
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

        # A second, shorter clip of the weather system: rain streaks, ripples and
        # the wind in the hair all advance with the frame clock.
        rseq = os.path.join(out, 'frames_rain')
        os.makedirs(rseq, exist_ok=True)
        rn = max(12, n // 2)
        rsz = max(240, asz * 3 // 4)
        print('[capture] rain animation: %d frames @ %d px' % (rn, rsz))
        run([binary, '--sequence', rseq, '--frames', str(rn), '--fps', str(args.fps),
             '--anim', 'sweep', '--size', str(rsz), '--samples', '2',
             '--preset', 'rain', '--wind', '0.8'])
        rpat = [os.path.join(rseq, 'frame_%04d.png' % i) for i in range(rn)]
        rpat = [p for p in rpat if os.path.exists(p)]
        if have('convert') and rpat:
            delay = max(2, int(round(100.0 / args.fps)))
            run(['convert', '-delay', str(delay), '-loop', '0'] + rpat +
                ['-colors', '256', os.path.join(out, 'rain.gif')])
            try:
                run(['convert', '-delay', str(delay), '-loop', '0'] + rpat +
                    ['-colors', '256', '-quality', '88', '-define', 'webp:method=4',
                     os.path.join(out, 'rain.webp')])
            except SystemExit:
                pass
            rjpgs = []
            for i, p in enumerate(rpat):
                j = os.path.join(rseq, 'frame_%04d.jpg' % i)
                run(['convert', p, '-quality', '92', j])
                rjpgs.append(j)
            write_mjpeg_avi(os.path.join(out, 'rain.avi'), rjpgs, args.fps)

    print('[capture] done -', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
