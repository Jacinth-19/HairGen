#!/bin/sh
# Bootstrap: create the meson build directory and compile.
#   ./setup.sh            -> build/ + build/hairgen
#   ./setup.sh --capture  -> also render the deliverable media into media/
set -e
cd "$(dirname "$0")"

MESON=${MESON:-meson}
NINJA=${NINJA:-ninja}
if ! command -v "$MESON" >/dev/null 2>&1; then
  if python3 -c 'import mesonbuild' 2>/dev/null; then
    MESON="python3 -m mesonbuild.mesonmain"
  else
    echo "meson not found - pip install meson ninja" >&2
    exit 1
  fi
fi
if ! command -v "$NINJA" >/dev/null 2>&1; then
  if python3 -c 'import ninja' 2>/dev/null; then NINJA="python3 -m ninja"; fi
fi

$MESON setup build --buildtype=release
$NINJA -C build

if [ "$1" = "--capture" ]; then
  python3 tools/capture.py --binary build/hairgen "$@"
fi
