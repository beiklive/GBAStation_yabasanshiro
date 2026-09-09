#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${ROOT_DIR}/yabause"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-switch}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN="${SOURCE_DIR}/src/nx/nx-toolchain.cmake"

command -v cmake >/dev/null || { echo "cmake not found" >&2; exit 1; }
command -v ninja >/dev/null || { echo "ninja not found" >&2; exit 1; }
test -x "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc" || {
  echo "Missing devkitA64 toolchain under ${DEVKITPRO}" >&2; exit 1;
}
test -f "${TOOLCHAIN}" || { echo "Missing Switch toolchain file" >&2; exit 1; }

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "${BUILD_DIR}"
fi

cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
  -DYAB_PORTS=nx -DYAB_MULTIBUILD=OFF \
  -DYAB_WANT_OPENGL=ON -DYAB_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel "${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

echo "Switch build complete"
find "${BUILD_DIR}" -maxdepth 2 -type f \( -name '*.nro' -o -name '*.nso' \) -print
