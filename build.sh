#!/bin/sh
# Build JUNO FM-1 on a Mac or Linux: run from the FM1 folder that holds fm1-base/ and juno-fm1/.
#   sh juno-fm1/build.sh          the firmware: juno-fm1/app/build/junofm1.fwsc
#   sh juno-fm1/build.sh host     the host simulator, then its checks
set -e
cd "$(dirname "$0")/.."
export PATH="$HOME/.docker/bin:$PATH"
export AC79_SDK="${AC79_SDK:-$PWD/fm1-base/.toolchain/ac79-sdk-full}"
PY=fm1-base/.venv/bin/python
[ -x "$PY" ] || PY=python3
if [ "$1" = host ]; then
    "$PY" fm1-base/tools/build.py juno-fm1/app --host
    for t in basic features; do
        juno-fm1/app/build/host/junofm1_host "juno-fm1/app/tests/$t.txt" /tmp/junoout
    done
else
    "$PY" fm1-base/tools/build.py juno-fm1/app
fi
