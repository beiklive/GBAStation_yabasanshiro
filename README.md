## GBAStation YabaSanshiro Switch core

This repository contains the standalone YabaSanshiro core used by
BeikliveStation/Tico on Nintendo Switch.

## Local Switch build

Run the build directly from an MSYS2 UCRT64 shell. Docker is only used by the
GitHub Actions workflow. Install devkitPro's Switch toolchain, CMake, Ninja and
Git in MSYS2, then run:

```bash
./build_local.sh -j "$(nproc)"
```

The output is `build-local/GBAStationYabaSanshiroStub.nro` (and the matching
`.nso`). Use `--clean` for a fresh build, or set `BUILD_DIR` to select another
output directory.
