# rxlibs design

Three native plugin libraries callable directly from CREXX Level B programs (no Python in between):

| plugin | namespace | content |
|---|---|---|
| `rxnumpy`  | `rxnumpy..np_*` | dense float64/int64 n-d arrays, broadcasting, reductions, sort, dot/matmul (n-d), slicing, fancy/boolean indexing, linalg, FFT, legacy MT19937 and `default_rng` (SeedSequence + PCG64, ziggurat) |
| `rxscipy`  | `rxscipy..sp_*` | special functions, normal/t distributions, trapz/simpson, interp, cubic spline, polyfit/polyval, pearsonr, ttest_ind, scalar math helpers |
| `rxspalg`  | `rxspalg..sp_*` | Level B module (source in `rxscipy/lib`) with callback algorithms: brentq, newton, quad, minimize (NM, BFGS), solve_ivp (RK45), curve_fit (LM) |
| `rxpandas` | `rxpandas..pd_*` | column table: CSV, filter, sort (multi-key), groupby (multi-key), merge inner/left/right/outer, value_counts, pivot, index, ISO datetime, describe |

## Layout

    core/            C89 libraries, no CREXX and no Python dependency
      rxnp.[ch]        arrays (elementwise, reductions, sort, matmul, slicing, parse/print)
      rxnp_la.c        linalg front end        rxnp_lapack.[ch]  LAPACK-style kernels (from p7_la.c, dgeev/dsyev/dgesdd ports)
      rxnp_fft.c       radix-2 + Bluestein FFT rxnp_rng.c        MT19937 legacy stream (np.random.seed/rand/randn/randint exact)
      rxnp_pcg.c       SeedSequence + PCG64 + Generator (tables rxnp_zig.inc from python/c/src/p7_rnd.c, 64-bit unsigned long required)
      rxnp_idx.c       comparison, take/where/compress/boolean get+set, unique, stack, n-d matmul/dot/tensordot
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

## Callbacks

A native plugin cannot call back into a Level B procedure, and Level B has no first-class procedure values, but it has **interfaces and classes**.
The convention (implemented in `rxscipy/lib/rxspalg.crexx`, namespace `rxspalg`): the solvers are Level B procedures written on top of the
rxnumpy/rxscipy plugins; the objective is an *object* implementing one of the library interfaces, and the solver calls its method:

| interface | method | used by |
|---|---|---|
| `.sp_f1`    | `f(x = .float) = .float` | `sp_brentq`, `sp_newton`, `sp_quad` |
| `.sp_f1d`   | `f(x)`, `df(x)` | `sp_newton_d` |
| `.sp_fn`    | `f(x = .binary) = .float` (x is an rxnumpy float64 vector) | `sp_minimize_nm`, `sp_minimize_bfgs` |
| `.sp_ode`   | `rhs(t = .float, y = .binary) = .binary` | `sp_solve_ivp` |
| `.sp_model` | `f(x = .float, p = .binary) = .float` | `sp_curve_fit` |

```rexx
import rxnumpy
import rxspalg
cube: class implements .sp_f1
  *: factory
    return
  f: method = .float
  arg x = .float
  return x * x * x - 2.0 * x - 5.0
main: procedure = .int
  say rxspalg..sp_brentq(.cube(), 2.0, 3.0, 0.000000000002, 0.000000000000000888, 100)   /* 2.094551... */
  return 0
```

Build and run: `scripts/buildlib.sh` compiles the module (`crexx -library rxspalg rxspalg.crexx -i<plugins>`) to `build/lib/rxspalg.rxbin`;
programs compile with `-i<build/lib>` and run with `-l<build/lib/rxspalg.rxbin>` (done by `scripts/run.sh`). Quirks found while writing it:
variables first assigned inside a `do` block are block local (assign before the loop when used after it), `//` starts a comment (not remainder), `fn` is not a usable variable name,
the driver refuses to build when a log file is written inside the compile directory.
Vectors cross the interface as rxnumpy handles (shared by reference: a callback must not keep or modify the vector it is given).
Algorithms follow scipy: brentq = `brentq.c`, newton = secant/Newton, `sp_quad` = QUADPACK qk21 error estimate with bisection of the worst interval,
Nelder-Mead = scipy's simplex (5% initial simplex, stable sort), `sp_solve_ivp` = RK45 with scipy's initial step, step control (safety 0.9, factors 0.2..10) and the 4th order
dense output (the default step sequence is identical to scipy's), `sp_curve_fit` = Levenberg-Marquardt with central-difference Jacobian and `pcov = inv(J'J) * cost/(m-n)`.

## Status

Working and tested against CPython + numpy 2.5.3 / scipy 1.18.1 / pandas 3.0.5 (`scripts/runtests.sh`, 8 programs): everything in the API tables.
Generator: seeds are non-negative ints (up to 64 bit) or int64 arrays (`np_default_rng_arr`); `np_rng_integers` has an exclusive high; `choice` is 1-d without `p`.
Masks are int64 0/1 arrays produced by `np_cmp`/`np_cmps` (`> < >= <= == !=`); `np_boolean_get/set`, `np_compress`, `np_take` (axis or flat), `np_where` with broadcasting.
rxpandas index: default RangeIndex (labels are positions; filter/sort/head keep the original positions as labels) or a labelled index from `pd_set_index`
(`pd_loc`, `pd_index_label`, `pd_reset_index`); datetimes are epoch seconds (float) in a numeric column (`pd_to_datetime`, `pd_dt_part` year/month/day/hour/minute/second/dow/doy; Monday = 0).
Not yet: complex dtype, int `axis` tuples, `Generator` distributions beyond normal/exponential/uniform, QUADPACK qagi/qags extrapolation, bounded/constrained optimisers,
stiff ODE solvers, categorical and string ops in rxpandas, timezone / non-ISO datetime parsing, `sp_interp1d` kinds other than linear (`sp_interp`) and cubic (`sp_cubic_spline`).

## Roadmap

1. Callback-free scipy: more distributions (chi2, gamma, beta, binom, poisson), `stats.describe`, `linregress`, `lstsq`, cubic spline, FFT helpers.
2. Bounded optimisers (L-BFGS-B), qagi, implicit ODE solvers, more Level B wrappers in `rxspalg`.
3. rxpandas: named aggregations, string column ops, `to_json`, `apply` via a Level B callbacks module.
4. Python-on-CREXX (`python/crexx`): optionally route `numpy`/`scipy`/`pandas` imports to these plugins (object wrapper over handles) so existing pure-Python pandas/scipy code in `pylib` runs on native kernels.
5. CMake integration (`add_dynamic_plugin_target`) once the libraries are placed inside a CREXX build.
