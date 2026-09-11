#!/usr/bin/env bash
# Local macOS desktop build of the GBAStation YabaSanshiro core.
#
# The desktop frontend renders the Saturn software output and the built-in
# boot page through SDL2 + OpenGL 3.3, so the emulator core itself is built
# without any OpenGL stack (YAB_WANT_OPENGL=OFF).
#
# Requirements:
#   brew install sdl2
# Output: build-macos/GBAStationYabaSanshiroDesktop  (or $BUILD_DIR when set)
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-macos}"
JOBS="${JOBS:-1}"
CLEAN=0

usage() {
  cat <<'EOF'
Usage: ./build_macos.sh [-j JOBS] [--clean]

Requires SDL2 (brew install sdl2) and CMake/Ninja.
Output: build-macos/GBAStationYabaSanshiroDesktop
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -j|--jobs) JOBS="${2:?missing job count}"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for tool in cmake ninja; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing $tool." >&2; exit 1; }
done

if command -v nproc >/dev/null 2>&1; then
  JOBS="${JOBS:-$(nproc)}"
else
  JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
fi

if [[ "$CLEAN" == 1 ]]; then
  rm -rf "$BUILD_DIR"
fi

cmake -Wno-dev -S "$ROOT/yabause" -B "$BUILD_DIR" -G Ninja \
  -DYAB_PORTS=desktop \
  -DYAB_WANT_OPENGL=OFF \
  -DYAB_WANT_SDL=OFF \
  -DYAB_WANT_C68K=FALSE \
  -DSH2_DYNAREC=FALSE \
  -DYAB_WANT_DYNAREC_DEVMIYAX=OFF \
  -DYAB_WANT_VULKAN=OFF \
  -DYAB_WANT_OPENAL=OFF \
  -DYAB_WANT_Q68=OFF \
  -DYAB_WANT_GDBSTUB=OFF

cmake --build "$BUILD_DIR" --target GBAStationYabaSanshiroDesktop --parallel "$JOBS"
BIN="$(find "$BUILD_DIR" -maxdepth 3 -type f \( -name 'yabasanshiro' -o -name 'GBAStationYabaSanshiroDesktop' \) -perm -111 2>/dev/null | head -n 1)"
test -n "$BIN"
echo "Output: $BIN"
