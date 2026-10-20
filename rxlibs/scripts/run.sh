#!/bin/sh
# run.sh prog.crexx [args] : compile and run a Level B program against the rxnumpy plugin
HERE=$(cd "$(dirname "$0")/.." && pwd)
CREXX_VM=${CREXX_VM:-${CREXX_ROOT:-/home/claude/src/crexx}/cmake-build-release/bin/crexx}
PD=${RXLIBS_PLUGIN_DIR:-$HERE/build/plugins}
prog=$1; shift
# work in a scratch dir: the driver writes .rxbin/.rxas next to the source
W=${RXLIBS_WORK:-$HERE/build/work}; mkdir -p "$W"; cp "$prog" "$W/"
cd "$W" || exit 1
LD=${RXLIBS_LIB_DIR:-$HERE/build/lib}
LARGS=""; for p in "$PD"/*.rxplugin; do LARGS="$LARGS -l$p"; done
IARGS="-i$PD"; if [ -d "$LD" ]; then IARGS="$IARGS -i$LD"; for p in "$LD"/*.rxbin; do LARGS="$LARGS -l$p"; done; fi
"$CREXX_VM" "$(basename "$prog")" $IARGS $LARGS "$@"
