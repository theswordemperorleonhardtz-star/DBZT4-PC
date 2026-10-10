#!/bin/sh
# Builds the Brazilian "DBZT4 1.8 (V2)" PC release from your own modded disc image.
#   port/release/bt4.sh <disc.iso>
# Needs python3 and the tools install.py checks for (7z, splat, the cross compilers); the last
# step needs docker, exactly as port/release/build.sh does.
set -e
ISO=${1:?usage: port/release/bt4.sh <disc.iso>}
cd "$(dirname "$0")/../.."

# 1) disc -> gamedata (SLUS_219.22, BT3PUS1/2/3.AFS, BIN/DBZP.BIN), splat split, build with the
#    real data tables and run the game's demo fight.  install.py is patched for this disc.
python3 install.py --iso "$ISO"

# 2) port/data: the shape of the data tables plus the checksum of THIS disc's values.  make_dat.py
#    bakes that checksum into <program>.dat, and port/src/plat_mem.c checks it at start.
python3 port/tools/make_skeleton.py

# 3) the release archives and the setup, built from the repository alone (BT3_SKELETON=1); no disc.
port/release/build.sh
echo "done: port/build_release/  (Tenkaichi3Decomp-{linux,windows}-x64.zip + setup)"
