#!/bin/sh
# Build the rxlibs plugins out of tree.
#   CREXX_ROOT   CREXX source tree providing rxpa/crexxpa.h   (default /home/claude/src/crexx)
#   CREXX_GENERATED  dir with crexx_version.h (default $CREXX_ROOT/cmake-build-release/generated)
#   RXLIBS_PLUGIN_DIR  output dir (default <rxlibs>/build/plugins)
# usage: build.sh [rxnumpy|rxscipy|rxpandas ...]   (default: all)
HERE=$(cd "$(dirname "$0")/.." && pwd)
CREXX_ROOT=${CREXX_ROOT:-/home/claude/src/crexx}
OUT=${RXLIBS_PLUGIN_DIR:-$HERE/build/plugins}
GEN=${CREXX_GENERATED:-$CREXX_ROOT/cmake-build-release/generated}
C="$HERE/core"
mkdir -p "$OUT" || exit 1
[ $# -eq 0 ] && set -- rxnumpy rxscipy rxpandas
NPCORE="$C/rxnp.c $C/rxnp_la.c $C/rxnp_lapack.c $C/rxnp_fft.c $C/rxnp_rng.c"
for p in "$@"; do
  case $p in
    rxnumpy)  SRC="$HERE/rxnumpy/plugin/rxnumpy.c $NPCORE" ;;
    rxscipy)  SRC="$HERE/rxscipy/plugin/rxscipy.c $C/rxsp.c $NPCORE" ;;
    rxpandas) SRC="$HERE/rxpandas/plugin/rxpandas.c $C/rxdf.c $NPCORE" ;;
    *) echo "unknown plugin $p"; exit 1 ;;
  esac
  ${CC:-gcc} -O2 -fPIC -shared -DBUILD_DLL -DPLUGIN_ID=$p \
    -I"$CREXX_ROOT/rxpa" -I"$GEN" -I"$C" $SRC -o "$OUT/$p.rxplugin" -lm || exit 1
  echo "built $OUT/$p.rxplugin"
done
