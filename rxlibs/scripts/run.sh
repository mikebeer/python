#!/bin/sh
# run.sh prog.crexx [args] : compile and run a Level B program against the rxnumpy plugin
HERE=$(cd "$(dirname "$0")/.." && pwd)
CREXX_VM=${CREXX_VM:-${CREXX_ROOT:-/home/claude/src/crexx}/cmake-build-release/bin/crexx}
PD=${RXLIBS_PLUGIN_DIR:-$HERE/build/plugins}
prog=$1; shift
# work in a scratch dir: the driver writes .rxbin/.rxas next to the source
W=${RXLIBS_WORK:-$HERE/build/work}; mkdir -p "$W"; cp "$prog" "$W/"
cd "$W" || exit 1
LARGS=""; for p in "$PD"/*.rxplugin; do LARGS="$LARGS -l$p"; done
"$CREXX_VM" "$(basename "$prog")" -i"$PD" $LARGS "$@"
