#!/bin/sh

set -o xtrace

mv GBAStationYabaSanshiroStub GBAStationYabaSanshiroStub.elf
elf2nro GBAStationYabaSanshiroStub.elf ../../GBAStationYabaSanshiroStub.nro
elf2nso GBAStationYabaSanshiroStub.elf ../../GBAStationYabaSanshiroStub.nso

#mkdir -p exefs
#cp yabasanshiro.nso ./exefs/main
#build_pfs0 exefs yabasanshiro.pfs0

