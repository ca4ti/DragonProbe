#!/bin/bash

set -eo pipefail

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SRCPATH=$(readlink -e "$SCRIPTPATH/..")
cd "$SRCPATH"
[ -d "cmake-build" ] || mkdir cmake-build
cd cmake-build

set -x
exec cmake -G Ninja -DBOARD=raspberry_pi_pico -DFAMILY=rp2040 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPICO_NO_FLASH=On -DUSE_USBCDC_FOR_STDIO=On -DUSE_SYSTEMWIDE_PICOSDK=On \
    "$SRCPATH"
