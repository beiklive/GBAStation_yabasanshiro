#!/usr/bin/env bash
set -euo pipefail

input_elf=${1:?missing input ELF}
output_nro=${2:?missing NRO output path}
output_nso=${3:?missing NSO output path}

elf2nro "$input_elf" "$output_nro"
elf2nso "$input_elf" "$output_nso"

#mkdir -p exefs
#cp yabasanshiro.nso ./exefs/main
#build_pfs0 exefs yabasanshiro.pfs0

