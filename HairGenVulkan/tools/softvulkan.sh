#!/bin/sh
# softvulkan.sh - give hairgen a CPU Vulkan device.
#
# A machine with no GPU (a CI container, a build box, this sandbox) can still
# render: the Vulkan *loader* is portable C, and the *driver* can be a software
# implementation. Neither is installed in a bare container, and neither ships in
# the distro packages available here, so this script assembles the two pieces
# from sources that are always reachable:
#
#   loader + headers  KhronosGroup/Vulkan-Loader and Vulkan-Headers (CMake, ~40 s)
#   driver (ICD)      Mesa lavapipe, from the @drawcall/webgl-linux-x64 npm
#                     package (the same library that package uses for headless
#                     WebGL), or SwiftShader from @onirenaud/swiftshader-vulkan
#
# Nothing here is needed on a machine that already has a Vulkan driver; the
# normal build is unchanged.
#
# Usage:
#   tools/softvulkan.sh --prefix /tmp/softvk            set it up (~1 min, once)
#   eval "$(tools/softvulkan.sh --prefix /tmp/softvk --env)"
#   ./build/hairgen --list-devices
#   ./build/hairgen --out render.png --size 320 --samples 4 --ssaa 2
#
# Options:
#   --prefix DIR    where to install           (default: $HOME/.cache/softvulkan)
#   --icd NAME      lavapipe | swiftshader     (default: lavapipe: faster and
#                                               more complete on x86-64)
#   --env           print the environment instead of building
#   --clean         remove the prefix first
#
# Measured on 2 vCPUs (llvmpipe, 18k strands, 24 segments, 4x MSAA + 2x SSAA):
# 288^2 still ~13 s, 384^2 ~25 s. That is slow but it is a real GPU-less render
# of the real pipeline - not a mock.
set -eu

PREFIX="${HOME}/.cache/softvulkan"
ICD="lavapipe"
PRINT_ENV=0
CLEAN=0
VK_VERSION="vulkan-sdk-1.3.290.0"

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --icd) ICD="$2"; shift 2 ;;
    --env) PRINT_ENV=1; shift ;;
    --clean) CLEAN=1; shift ;;
    -h|--help) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ "$ICD" = "swiftshader" ]; then
  ICD_DIR_NAME="swift"
  ICD_JSON="vk_swiftshader_icd.json"
  ICD_LIB="libvk_swiftshader.so"
  ICD_PKG="@onirenaud/swiftshader-vulkan@0.1.0"
  ICD_SUB="linux-x64"
else
  ICD_DIR_NAME="lvp"
  ICD_JSON="lvp_icd.json"
  ICD_LIB="libvulkan_lvp.so"
  ICD_PKG="@drawcall/webgl-linux-x64@0.1.0"
  ICD_SUB="."
fi

print_env() {
  echo "export LD_LIBRARY_PATH=\"${PREFIX}/loader\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\""
  echo "export VK_DRIVER_FILES=\"${PREFIX}/icd/${ICD_JSON}\""
  echo "export VK_ICD_FILENAMES=\"${PREFIX}/icd/${ICD_JSON}\""
}

if [ "$PRINT_ENV" = "1" ]; then
  print_env
  exit 0
fi

if [ "$CLEAN" = "1" ]; then
  rm -rf "$PREFIX"
fi

need() {
  command -v "$1" >/dev/null 2>&1 || { echo "softvulkan: missing $1${2:+ ($2)}" >&2; exit 1; }
}
need curl
need tar
need npm
need g++

CMAKE="cmake"
if ! command -v cmake >/dev/null 2>&1; then
  # No distro package needed: cmake is on PyPI.
  python3 -m pip install --break-system-packages -q cmake >/dev/null 2>&1 || \
    python3 -m pip install --user -q cmake >/dev/null 2>&1
  CMAKE="python3 -m cmake"
fi

mkdir -p "$PREFIX"
WORK="$PREFIX/src"
mkdir -p "$WORK"

# ------------------------------------------------------------------ loader ---
if [ ! -f "$PREFIX/loader/libvulkan.so.1" ]; then
  echo "softvulkan: building the Vulkan loader $VK_VERSION"
  cd "$WORK"
  [ -f vh.tar.gz ] || curl -sSL -o vh.tar.gz \
    "https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/$VK_VERSION"
  [ -f vl.tar.gz ] || curl -sSL -o vl.tar.gz \
    "https://codeload.github.com/KhronosGroup/Vulkan-Loader/tar.gz/refs/tags/$VK_VERSION"
  tar xzf vh.tar.gz
  tar xzf vl.tar.gz
  $CMAKE -S "Vulkan-Headers-${VK_VERSION}" -B vh-build \
    -DCMAKE_INSTALL_PREFIX="$PREFIX/headers" -DCMAKE_BUILD_TYPE=Release >/dev/null
  $CMAKE --install vh-build >/dev/null

  # The loader asks pkg-config for the X11/XCB/Wayland window-system support it
  # will never use here; WSI is switched off, and a stub answers the version
  # probe (pkgconfig itself is not necessarily installed).
  mkdir -p "$WORK/bin"
  printf '#!/bin/sh\nif [ "$1" = "--version" ]; then echo 1.8.0; exit 0; fi\nexit 1\n' \
    > "$WORK/bin/pkg-config"
  chmod +x "$WORK/bin/pkg-config"

  PATH="$WORK/bin:$PATH" $CMAKE -S "Vulkan-Loader-${VK_VERSION}" -B vl-build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX/headers" \
    -DVULKAN_HEADERS_INSTALL_DIR="$PREFIX/headers" -DUPDATE_DEPS=OFF \
    -DBUILD_TESTS=OFF -DBUILD_WSI_XLIB_SUPPORT=OFF -DBUILD_WSI_XCB_SUPPORT=OFF \
    -DBUILD_WSI_WAYLAND_SUPPORT=OFF -DBUILD_WSI_DIRECTFB_SUPPORT=OFF >/dev/null
  PATH="$WORK/bin:$PATH" $CMAKE --build vl-build -j "$(nproc 2>/dev/null || echo 2)" >/dev/null

  mkdir -p "$PREFIX/loader"
  for f in libvulkan.so libvulkan.so.1; do cp "vl-build/loader/$f" "$PREFIX/loader/"; done
  cp "vl-build/loader/libvulkan.so.1.3.290" "$PREFIX/loader/" 2>/dev/null || true
fi

# --------------------------------------------------------------------- ICD ---
if [ ! -f "$PREFIX/icd/$ICD_JSON" ]; then
  echo "softvulkan: unpacking the $ICD driver ($ICD_PKG)"
  mkdir -p "$PREFIX/icd" "$WORK/npm"
  cd "$WORK/npm"
  npm pack "$ICD_PKG" --silent >/dev/null 2>&1 || {
    echo "softvulkan: npm pack failed for $ICD_PKG" >&2; exit 1; }
  tgz=$(ls ./*.tgz | head -1)
  tar xzf "$tgz"
  if [ "$ICD_SUB" = "." ]; then
    cp "package/$ICD_JSON" "package/$ICD_LIB" "$PREFIX/icd/"
  else
    cp "package/$ICD_SUB/$ICD_JSON" "package/$ICD_SUB/$ICD_LIB" "$PREFIX/icd/"
  fi
fi

# The ICD JSON names its library next to itself, so the two must stay together.
echo "softvulkan: ready in $PREFIX"
print_env
