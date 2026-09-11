#!/bin/sh

set -o xtrace

romfs_dir=${1:-}

mv yabasanshiro yabasanshiro.elf
if [ -n "$romfs_dir" ] && [ -d "$romfs_dir" ]; then
  elf2nro yabasanshiro.elf ../../GBAStationYabaSanshiroStub.nro --romfsdir="$romfs_dir"
else
  elf2nro yabasanshiro.elf ../../GBAStationYabaSanshiroStub.nro
fi
elf2nso yabasanshiro.elf ../../GBAStationYabaSanshiroStub.nso

#mkdir -p exefs
#cp yabasanshiro.nso ./exefs/main
#build_pfs0 exefs yabasanshiro.pfs0
