#!/bin/sh
# The GitHub Pages site (the gh-pages branch): fm1-base's site (front page, web installer, firmware) and play/, the
# firmware in the browser through fm1-emu. Run from the folder that holds fm1-base/ and this repository, after a
# release build (python3 fm1-base/tools/build.py juniper-fm1/app --release X.Y):
#   sh juniper-fm1/tools/make_pages.sh X.Y        -> juniper-fm1/site/
# fm1-base's make_site.py also wants the Emscripten emulator (build.py --web); without Emscripten a stand-in lets it
# run, and play/ (fm1-emu) stands in for the emulator.
set -e
V="$1"
[ -n "$V" ] || { echo "usage: sh juniper-fm1/tools/make_pages.sh X.Y"; exit 2; }
ME="$(basename "$(cd "$(dirname "$0")/.." && pwd)")"
cd "$(dirname "$0")/../.."
PY=fm1-base/.venv/bin/python
[ -x "$PY" ] || PY=python3
EMU=$ME/app/build/emu
STUB=
if [ ! -f "$EMU/app.wasm" ]; then
    mkdir -p "$EMU" && : > "$EMU/app.wasm" && STUB=1
fi
"$PY" fm1-base/tools/make_site.py "$ME/app" "$ME/app/build/juniperfm1-$V.fwsc" "$V" "$ME/site"
if [ -n "$STUB" ]; then
    rm -rf "$ME/site/emu" "$EMU"
fi
mkdir -p "$ME/site/play"
sed "s/^const VERSION = \".*\";/const VERSION = \"$V\";/" "$ME/app/site/play.html" > "$ME/site/play/index.html"
echo "pages: $ME/site (play/ plays juniperfm1-$V.fwsc)"
