#!/bin/sh
# Build JUNIPER FM-1 on a Mac or Linux: run from the folder that holds fm1-base/ and this repository (juniper-fm1/).
#   sh juniper-fm1/build.sh          the firmware: juniper-fm1/app/build/juniperfm1.fwsc
#   sh juniper-fm1/build.sh host     the host simulator, then its checks
set -e
ME="$(basename "$(cd "$(dirname "$0")" && pwd)")"   # this folder's name, whatever it was cloned as
cd "$(dirname "$0")/.."
export PATH="$HOME/.docker/bin:$PATH"
export AC79_SDK="${AC79_SDK:-$PWD/fm1-base/.toolchain/ac79-sdk-full}"
PY=fm1-base/.venv/bin/python
[ -x "$PY" ] || PY=python3
if [ "$1" = host ]; then
    "$PY" fm1-base/tools/build.py "$ME/app" --host
    for t in basic features arp_patch sysex_slot; do
        "$ME/app/build/host/juniperfm1_host" "$ME/app/tests/$t.txt" /tmp/juniperout
    done
else
    "$PY" fm1-base/tools/build.py "$ME/app"
fi
