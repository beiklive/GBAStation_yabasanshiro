#!/usr/bin/env bash
# Local Nintendo Switch build. Run this from an MSYS2 UCRT64 shell.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-local}"
JOBS="${JOBS:-$(nproc)}"
CLEAN=0

if [[ "$(uname -o 2>/dev/null || true)" == "Msys" ]]; then
  export PATH="/usr/bin:/bin:$PATH"
fi

usage() {
  cat <<'EOF'
Usage: ./build_local.sh [-j JOBS] [--clean]

Requires MSYS2 with devkitPro Switch tools, CMake, Ninja and Git.
Output: build-local/GBAStationYabaSanshiroStub.nro (or $BUILD_DIR when set).
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

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
for tool in cmake ninja git; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing $tool in MSYS2." >&2; exit 1; }
done
[[ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc" ]] || {
  echo "Missing devkitPro Switch toolchain under $DEVKITPRO." >&2; exit 1;
}

if [[ "$CLEAN" == 1 ]]; then
  rm -rf "$BUILD_DIR"
fi

cmake -Wno-dev -S "$ROOT/yabause" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=src/nx/nx-toolchain.cmake \
  -DYAB_PORTS=nx \
  -DYAB_WANT_C68K=FALSE \
  -DSH2_DYNAREC=FALSE \
  -DYAB_WANT_DYNAREC_DEVMIYAX=OFF
cmake --build "$BUILD_DIR" --target GBAStationYabaSanshiroStub --parallel "$JOBS"
test -s "$BUILD_DIR/GBAStationYabaSanshiroStub.nro"
echo "Output: $BUILD_DIR/GBAStationYabaSanshiroStub.nro"
