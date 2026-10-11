# Startup time of `crexx/py`

## Problem
`./py hello.py` took 1.6-2.6 s. Profile (callgrind on `rxvme py.rxbin`, 1.6 G instructions):

| where | share |
|---|---|
| `rxvm_rebuild_interface_method_registry` / `_factory_registry` (during `rxvm_link`) | 58 % |
| of that `resolve_runtime_procedure` (linear scan of every module's meta list) | 30 % |
| `runtime_proc_matches_signature` (another linear scan) | 16 % |
| `rxbin` reading of py.rxbin + ~280 autoloaded library modules | 22 % |
| actually running the program | 5 % |

The registry rebuild loops over every `.implements` meta and, for each, scans every meta of every module
(looking for interface members, then again for each procedure name and each signature check).
`py.rxas` has ~13,000 `.meta` entries (about 6,400 are per-local debug metas, ~960 globals, 457 procedures), so the
work is (implements entries) x (all metas), several times over.

## What was tried on our side
The compiler offers no switch to drop metas, and only the 457 `META_FUNC` entries matter for the lookups; the cost is the
scan, not the count, so shrinking the program would only scale a quadratic cost. There is no pre-linked image: `py.rxbin`
is already the assembled form, and linking (including the registry rebuild) happens on every load. So the fix is in the VM.

## Fix (`vm-startup.patch`, touches only `interpreter/rxvmintp.c`)
While a registry rebuild runs, a hash index is built once from the meta lists: `META_FUNC` by lower-cased name and by
procedure pointer, `META_MEMBER` by owner, `META_IMPLEMENTS` by (class, interface). `resolve_runtime_procedure`,
`runtime_proc_matches_signature`, `runtime_class_implements_interface` and the member search in the two rebuild functions use it
(first match wins, as before). The index is freed at the end of the rebuild; outside a rebuild the old linear code is used.
Registry contents are unchanged.

## Numbers (this machine, 2 cores)
| | stock VM | patched VM |
|---|---|---|
| `print(1)` | 1.6-2.2 s (2.6 s first measurement) | 0.41-0.49 s |
| instructions (callgrind) | 1.60 G | 0.7 G |
| 21-test suite (`tests/run_tests.sh ./crexx/py`) | 75 s | 34 s |

All 21 tests pass with both. The remaining 0.4 s is mostly reading the 2.7 MB py.rxbin plus the library modules.

## Apply and use
    cd /path/to/crexx && git apply /home/claude/python/crexx/vm-startup.patch   # tested against 77ba820c
    # rebuild rxvme the usual way (cmake --build ... --target rxvme; the driver finds <dir>/../bin/rxvme)
    export CREXX_VM=/path/to/patched/bin/crexx
    ./crexx/py script.py

The launcher `py` uses `CREXX_VM` if set, else `CREXX`, else `crexx` (default unchanged). The patched `bin/` directory must be a
complete build tree (the driver locates `rxvme` and the providers/plugins next to itself); the original source tree was not modified.
