# rxlibs design

Three native plugin libraries callable directly from CREXX Level B programs (no Python in between):

| plugin | namespace | content |
|---|---|---|
| `rxnumpy`  | `rxnumpy..np_*` | dense float64/int64 n-d arrays, broadcasting, reductions, sort, dot, slicing, linalg (solve/inv/det/cholesky/qr/eigh/eigvals/svd), FFT, legacy MT19937 random |
| `rxscipy`  | `rxscipy..sp_*` | special functions, normal/t distributions, trapz/simpson, interp, polyfit/polyval, pearsonr, ttest_ind |
| `rxpandas` | `rxpandas..pd_*` | column table: read/write CSV, filter, sort, groupby/agg, merge, describe, head, select, printing |

## Layout

    core/            C89 libraries, no CREXX and no Python dependency
      rxnp.[ch]        arrays (elementwise, reductions, sort, matmul, slicing, parse/print)
      rxnp_la.c        linalg front end        rxnp_lapack.[ch]  LAPACK-style kernels (from p7_la.c, dgeev/dsyev/dgesdd ports)
      rxnp_fft.c       radix-2 + Bluestein FFT rxnp_rng.c        MT19937 legacy stream (np.random.seed/rand/randn/randint exact)
      rxsp.[ch]        scipy-style numerics    rxdf.[ch]         table
      rxlibs_handle.h  opaque handle glue shared by the plugins (needs crexxpa.h)
    rxnumpy/ rxscipy/ rxpandas/   plugin/<name>.c (rxpa wrapper), examples/, tests/ (+ bench/)
    scripts/         build.sh  run.sh  runtests.sh  gen_api.py

## Handle model

A handle is a CREXX `.binary` value that carries a *native payload* `{magic, void *obj}` with
`rxpa_native_payload_ops {type_name, copy, finalize}` (see `rxpa/crexxpa.h`).

* The object is reference counted (`refs` first member). `copy` (VM copies the value, e.g. assignment into
  another variable or argument passing) retains, `finalize` releases; the object is freed when the last
  CREXX value dies. There is no explicit free call.
* Copies **share** the object (reference semantics, like Python/numpy): `h = g; np_set(h,0,9.0)` changes `g`.
  `np_copy` makes a deep copy. All functions that produce arrays return a *new* handle; the inputs are never mutated
  except by `np_set`, `np_set2` and `pd_add_col`/`pd_add_str_col`.
* Type check: a function accepts a handle only if the payload `type_name` matches
  (`crexx.rxnumpy.array.v1`, `crexx.rxpandas.frame.v1`). rxscipy and rxpandas accept rxnumpy arrays by name;
  all three plugins compile the same `rxnp.c`, so the struct layout is shared and `free()` is the common libc one.
  (Constraint: a handle must not outlive the plugin library that created it; the VM keeps plugins loaded for the process.)
* Out parameters (`np_qr`, `np_eigh`, `np_svd`, `sp_pearsonr`, `sp_ttest_ind`) use CREXX `expose` arguments:
  declare `q = .binary` / `r = 0.0` first and pass the variable.
* Shapes are passed as strings: `"3,4"`, `"(3, 4)"`; reshape allows one `-1`.
  `np_array` takes a Python-like nested list literal (`"[[1, 2], [3.5, 4]]"`); all-integer text gives int64, otherwise float64.
  `np_tolist` returns the same notation (Python `repr` of floats, so it round-trips and matches `ndarray.tolist()` text).
* Complex FFT results are real arrays of shape `(n,2)` = `[re, im]`; `np_ifft` accepts shape `(n)` or `(n,2)`.
  `np_eigvals` returns `(n,2)` likewise.

## Error signalling

Failures raise CREXX signals via `RETURNSIGNAL`: `INVALID_ARGUMENTS` (bad handle, shape mismatch, singular matrix,
unknown column ...), `OUT_OF_RANGE` (index/row/column index), `FAILURE` (allocation, handle publication).
Level B programs catch them with `do ... on signal invalid_arguments as problem ... end`;
`problem.message()` carries the text from the C library (e.g. `Singular matrix`).

## Memory

Core allocations use malloc; every core function returns either a new object (refs = 1, ownership passes to the
plugin which publishes it as a handle) or NULL with a message in `rxnp_error()`, `rxnp_la_error()`, `rxsp_error()`, `rxdf_error()`
(per-library static buffers; the plugins are declared process-reentrant but the error buffers and the global RNG are not thread safe: documented limitation).

## Build / install

`scripts/build.sh [rxnumpy|rxscipy|rxpandas]` compiles each plugin out of tree with the system C compiler:
`cc -shared -fPIC -DBUILD_DLL -DPLUGIN_ID=<name> -I$CREXX_ROOT/rxpa -I$CREXX_GENERATED -Icore ...`, producing `<name>.rxplugin`
(the file the VM loads; `PLUGIN_ID` must equal the namespace). Environment: `CREXX_ROOT` (source tree, default
`/home/claude/src/crexx`), `CREXX_GENERATED` (dir containing `crexx_version.h`), `RXLIBS_PLUGIN_DIR` (output, default `build/plugins`).
Only the public `rxpa` headers are used; the CREXX tree is not modified. The plugin must be built against headers of the
same CREXX version as the VM that loads it (the patched and the stock VM share the plugin ABI v1/v2).

Using a private plugin directory (no installation into the CREXX `bin/`):

* compile: `crexx prog.crexx -i<plugin dir>`      (rxc reads the declarations from the `.rxplugin`)
* run:     `-l<plugin dir>/rxnumpy.rxplugin`      (one `-l` per plugin; the VM loads the provider)

`scripts/run.sh prog.crexx` does both for every plugin in the directory (set `CREXX_VM` to the driver).
Level B programs must put their code in `main: procedure = .int` when they define helper procedures.
Variables first assigned inside a `do` block are local to the block.

## Callbacks (not yet): reverse communication

A native plugin cannot call back into a Level B procedure. Functions that need an objective (`minimize`, `root`,
`quad`, `solve_ivp`, `curve_fit`) are therefore planned either as (a) Level B modules written on top of rxnumpy
(algorithms in CREXX calling the user's own procedure, vector work in C) or (b) *ask/tell* solvers in C:
`h = sp_nm_new(x0)`, loop `x = sp_nm_ask(h)`, user evaluates `f(x)`, `call sp_nm_tell(h, fx)`, until `sp_nm_done(h)`.

## Status

Working and tested against CPython + numpy 2.5.3 / scipy 1.18.1 / pandas 3.0.5 (`scripts/runtests.sh`, 5 programs):
everything in the API tables. Not yet: PCG64/`default_rng` (code exists in `python/c/src/p7_rnd.c`, to be factored),
n-d (>2) matmul, fancy/boolean indexing, complex dtype, int `axis` tuples, rxscipy optimize/integrate.quad/ode/signal/sparse,
rxpandas datetime, categorical, multi-key groupby, pivot, string ops, index labels.

## Roadmap

1. Callback-free scipy: more distributions (chi2, gamma, beta, binom, poisson), `stats.describe`, `linregress`, `lstsq`, cubic spline, FFT helpers.
2. Ask/tell optimizers and ODE steppers; Level B convenience wrappers.
3. rxpandas: multi-key groupby, named aggregations, string columns ops, `to_json`, index, `apply` via Level B callbacks module.
4. Python-on-CREXX (`python/crexx`): optionally route `numpy`/`scipy`/`pandas` imports to these plugins (object wrapper over handles) so existing pure-Python pandas/scipy code in `pylib` runs on native kernels.
5. CMake integration (`add_dynamic_plugin_target`) once the libraries are placed inside a CREXX build.
