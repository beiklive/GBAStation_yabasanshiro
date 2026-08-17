#!/usr/bin/env bash
# Build the YabaSanshiro external core as a Switch NRO.
# Requires Docker; devkitPro's devkita64 image supplies the cross toolchain.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
IMAGE="${IMAGE:-devkitpro/devkita64:latest}"
BUILD_DIR="${BUILD_DIR:-build-switch}"

# Docker Desktop bind mounts occasionally produce a corrupt archive when the
# legacy libchdr/zstd ExternalProject builds in parallel.  Keep Windows builds
# deterministic; Linux users and CI can still override JOBS when desired.
if [ -z "${JOBS:-}" ]; then
  if command -v cygpath >/dev/null 2>&1; then
    JOBS=1
  else
    JOBS=2
  fi
fi

if command -v docker >/dev/null 2>&1; then
  DOCKER=(docker)
elif [ -x "/c/Program Files/Docker/Docker/resources/bin/docker.exe" ]; then
  DOCKER=("/c/Program Files/Docker/Docker/resources/bin/docker.exe")
else
  echo "Docker is required to build the Switch core." >&2
  exit 1
fi

MOUNT_ROOT="$ROOT"
CONTAINER_BUILD_DIR="$BUILD_DIR"
if command -v cygpath >/dev/null 2>&1; then
  MOUNT_ROOT="$(cygpath -w "$ROOT")"
  # Build outside the Docker Desktop bind mount.  GNU ar/ranlib can otherwise
  # observe partially-written archives in libchdr's nested ExternalProject.
  CONTAINER_BUILD_DIR="/tmp/gbastation-yabasanshiro-build"
fi

if [ "${1:-}" = "clean" ]; then
  rm -rf "$ROOT/$BUILD_DIR"
  echo "Removed $BUILD_DIR"
  exit 0
fi

MSYS_NO_PATHCONV=1 "${DOCKER[@]}" run --rm \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -e BUILD_DIR="$BUILD_DIR" \
  -e CONTAINER_BUILD_DIR="$CONTAINER_BUILD_DIR" \
  -e JOBS="$JOBS" \
  -v "$MOUNT_ROOT:/project/source" \
  --workdir /project/source \
  "$IMAGE" \
  bash -lc '
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEBIAN_FRONTEND=noninteractive

apt-get update >/dev/null
apt-get install -y --no-install-recommends cmake git ninja-build >/dev/null

git config --global --add safe.directory /project/source

cmake -Wno-dev -S yabause -B "$CONTAINER_BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=src/nx/nx-toolchain.cmake \
  -DYAB_PORTS=nx \
  -DYAB_WANT_C68K=FALSE \
  -DSH2_DYNAREC=FALSE \
  -DYAB_WANT_DYNAREC_DEVMIYAX=OFF

cmake --build "$CONTAINER_BUILD_DIR" --target GBAStationYabaSanshiroStub --parallel "$JOBS"
test -s "$CONTAINER_BUILD_DIR/GBAStationYabaSanshiroStub.nro"

if [ "$CONTAINER_BUILD_DIR" != "$BUILD_DIR" ]; then
  mkdir -p "/project/source/$BUILD_DIR"
  cp "$CONTAINER_BUILD_DIR/GBAStationYabaSanshiroStub.nro" "/project/source/$BUILD_DIR/"
  cp "$CONTAINER_BUILD_DIR/GBAStationYabaSanshiroStub.nso" "/project/source/$BUILD_DIR/"
fi
chown -R "$HOST_UID:$HOST_GID" "/project/source/$BUILD_DIR"
'

echo "Output: $ROOT/$BUILD_DIR/GBAStationYabaSanshiroStub.nro"
