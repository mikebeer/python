#!/bin/sh
# Compile the Level B algorithm libraries (rxscipy/lib/*.crexx) to build/lib/<name>.rxbin.
# needs the plugins built first (scripts/build.sh).
HERE=$(cd "$(dirname "$0")/.." && pwd)
CREXX_VM=${CREXX_VM:-${CREXX_ROOT:-/home/claude/src/crexx}/cmake-build-release/bin/crexx}
PD=${RXLIBS_PLUGIN_DIR:-$HERE/build/plugins}
LD=$HERE/build/lib; W=$HERE/build/libwork
mkdir -p "$LD" "$W"
for src in "$HERE"/rx*/lib/*.crexx; do
  n=$(basename "$src" .crexx); cp "$src" "$W/"
  (cd "$W" && "$CREXX_VM" -library "$n" "$n.crexx" -i../plugins -i../lib > "$HERE/build/$n.buildlog" 2>&1) || { cat "$HERE/build/$n.buildlog"; exit 1; }
  cp "$W/$n.rxbin" "$LD/$n.rxbin"; echo "built $LD/$n.rxbin"
done
