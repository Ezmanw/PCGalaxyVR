#!/bin/bash
# Smoke test of a Linux build without a headset or game files (CI):
#   tools/ci_smoke_test.sh build/pcgalaxyvr
# 1. The renderer's and VR layer's shaders, and a spread of generated GX
#    shaders, build on desktop OpenGL (Mesa's software renderer in CI).
# 2. The headless runner maps the Wii's memory at its addresses, then stops
#    at the disc check with a clear message.
set -euo pipefail
BIN=${1:-build/pcgalaxyvr}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
export XDG_DATA_HOME=$WORK/data XDG_CONFIG_HOME=$WORK/config XDG_CACHE_HOME=$WORK/cache PETARI_NOAUDIO=1

"$BIN" --gl-selftest 256

mkdir -p "$WORK/empty"
status=0
"$BIN" --headless "$WORK/empty" "$WORK/saves" 5 > "$WORK/log" 2>&1 || status=$?
cat "$WORK/log"
grep -q "mem: MEM1 80000000" "$WORK/log"
grep -q "not the files of a Super Mario Galaxy disc" "$WORK/log"
echo "smoke test passed (headless runner exit status $status, as expected without game files)"
