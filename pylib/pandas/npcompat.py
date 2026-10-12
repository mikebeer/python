# make numpy functions accept pandas objects (the native numpy subset only knows lists / ndarrays)
import numpy as np
from .core import Series, DataFrame, ObjArray
from .base import Index

_UFUNCS = ('sqrt exp log log10 log2 log1p expm1 sin cos tan arcsin arccos arctan sinh cosh tanh floor ceil trunc sign '
           'square negative reciprocal isnan isinf isfinite abs absolute rint fabs deg2rad rad2deg cbrt exp2 '
           'logical_not nan_to_num').split()
_BINARY = ('maximum minimum add subtract multiply divide true_divide power mod floor_divide fmax fmin hypot arctan2 '
           'greater less equal not_equal greater_equal less_equal logical_and logical_or logical_xor isclose').split()
_METHODS = ('sum mean median min max prod std var any all cumsum cumprod argmax argmin ptp').split()
_ALIAS = {'amax': 'max', 'amin': 'min', 'nansum': 'sum', 'nanmean': 'mean', 'nanmax': 'max', 'nanmin': 'min',
          'nanmedian': 'median', 'nanstd': 'std', 'nanvar': 'var', 'average': 'mean', 'product': 'prod'}
_PLAIN = ('array asarray asanyarray where concatenate hstack vstack stack column_stack dstack dot matmul sort unique '
          'argsort diff corrcoef cov percentile quantile histogram digitize isin searchsorted trapezoid polyfit '
          'polyval interp count_nonzero bincount cross outer inner array_equal allclose clip round around '
          'nanpercentile nanquantile select choose take flip roll tile repeat append delete '
          'insert meshgrid linalg_norm').split()


def _cv(a):
    if isinstance(a, Series):
        return a.values if a._dt in ('int64', 'float64', 'bool', 'int32') else a._v
    if isinstance(a, Index):
        return a.values if a._dt in ('int64', 'float64', 'bool', 'int32') else a._v
    if isinstance(a, DataFrame):
        return a.to_numpy()
    if isinstance(a, (list, tuple)):
        for x in a:
            if isinstance(x, (Series, Index, DataFrame)):
                return [_cv(y) for y in a]
        return a
    return a


def _first_pd(args):
    for a in args:
        if isinstance(a, (Series, DataFrame)):
            return a
    return None


def _wrap_result(r, src):
    if src is None or not isinstance(r, np.ndarray):
        return r
    if isinstance(src, Series):
        if r.ndim == 1 and r.shape[0] == len(src._v):
            return Series(r.tolist(), src._idx, name=src.name)
        return r
    if r.ndim == 2 and r.shape == src.shape:
        return DataFrame(r, index=src._idx, columns=src._cols)
    return r


def _make_elem(orig):
    def f(*args, **kw):
        src = _first_pd(args)
        if src is None:
            return orig(*args, **kw)
        r = orig(*[_cv(a) for a in args], **kw)
        return _wrap_result(r, src)
    return f


def _make_plain(orig):
    def f(*args, **kw):
        found = False
        for a in args:
            if isinstance(a, (Series, DataFrame, Index, list, tuple)):
                found = True
        if not found:
            return orig(*args, **kw)
        return orig(*[_cv(a) for a in args], **kw)
    return f


def _make_method(orig, mname):
    def f(a, *args, **kw):
        if isinstance(a, (Series, DataFrame)):
            m = getattr(a, mname)
            if mname in ('std', 'var') and 'ddof' not in kw:
                kw['ddof'] = 0
            for k in ('dtype', 'out', 'keepdims'):
                if k in kw and kw[k] is None:
                    del kw[k]
            return m(*args, **kw)
        return orig(_cv(a), *args, **kw)
    return f


def install():
    for nm in _UFUNCS + _BINARY:
        try:
            o = getattr(np, nm)
        except Exception:
            continue
        setattr(np, nm, _make_elem(o))
    for nm in _METHODS:
        try:
            o = getattr(np, nm)
        except Exception:
            continue
        setattr(np, nm, _make_method(o, nm))
    for nm in _ALIAS:
        try:
            o = getattr(np, nm)
        except Exception:
            continue
        setattr(np, nm, _make_method(o, _ALIAS[nm]))
    for nm in _PLAIN:
        try:
            o = getattr(np, nm)
        except Exception:
            continue
        if nm in ('round', 'around', 'clip'):
            setattr(np, nm, _make_elem(o))
        else:
            setattr(np, nm, _make_plain(o))
