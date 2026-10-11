# pandas subset: Series and DataFrame (pure Python on top of the native numpy subset)
import math
import numpy as np
from .base import (nan, NA, NaT, Dtype, dtype_of, norm_dtype, infer, Index, RangeIndex, MultiIndex,
                   isna_scalar, _astype_list, to_np, sortkey_list)
from . import fmt as _fmt

_NUMDT = ('int64', 'float64', 'bool')


class ObjArray:
    """stand-in for numpy object arrays (the native numpy only has int/float/bool)"""

    def __init__(self, a):
        self._a = a

    @property
    def shape(self):
        s = []
        x = self._a
        while isinstance(x, list):
            s.append(len(x))
            if len(x) == 0:
                break
            x = x[0]
        return tuple(s)

    @property
    def ndim(self):
        return len(self.shape)

    @property
    def size(self):
        n = 1
        for k in self.shape:
            n *= k
        return n

    @property
    def dtype(self):
        return dtype_of('object')

    @property
    def T(self):
        if self.ndim == 2:
            return ObjArray([list(r) for r in zip(*self._a)])
        return self

    def tolist(self):
        return [list(r) if isinstance(r, list) else r for r in self._a]

    def __len__(self):
        return len(self._a)

    def __iter__(self):
        return iter(self._a)

    def __getitem__(self, k):
        if isinstance(k, tuple):
            r = self._a[k[0]] if not isinstance(k[0], slice) else self._a[k[0]]
            if isinstance(k[0], slice):
                return ObjArray([x[k[1]] for x in r])
            return r[k[1]]
        r = self._a[k]
        if isinstance(k, slice):
            return ObjArray(r)
        return r

    def copy(self):
        return ObjArray(self.tolist())

    def astype(self, dt):
        dn = norm_dtype(dt)
        if dn == 'float64':
            return np.array(_conv_nested(self._a, float))
        if dn == 'int64':
            return np.array(_conv_nested(self._a, int))
        return self

    def flatten(self):
        if self.ndim == 2:
            return ObjArray([x for r in self._a for x in r])
        return self

    def __eq__(self, o):
        if isinstance(o, ObjArray):
            return self._a == o._a
        return np.array([x == o for x in self._a])

    def __ne__(self, o):
        r = self.__eq__(o)
        if isinstance(r, bool):
            return not r
        return ~r

    def __hash__(self):
        return id(self)

    def __repr__(self):
        if self.ndim == 2:
            return '[' + '\n '.join(['[' + ' '.join([repr(x) for x in r]) + ']' for r in self._a]) + ']'
        return '[' + ' '.join([repr(x) for x in self._a]) + ']'

    __str__ = __repr__


def _conv_nested(a, f):
    if len(a) > 0 and isinstance(a[0], list):
        return [_conv_nested(r, f) for r in a]
    return [f(x) for x in a]


def _lst(x):
    """list-like -> python list (may share storage, treat as read only)"""
    if isinstance(x, list):
        return x
    if isinstance(x, (Series, Index)):
        return x._v
    if isinstance(x, np.ndarray):
        return x.tolist()
    if isinstance(x, ObjArray):
        return x._a
    if isinstance(x, DataFrame):
        return x._vals[0]
    return list(x)


_ITERS = ('generator', 'iterator', 'map', 'filter', 'zip', 'enumerate', 'reversed', 'dict_keys', 'dict_values', 'dict_items', 'dict_valueiterator', 'list_iterator')


def _is_iter(x):
    return type(x).__name__ in _ITERS


def _is_listlike(x):
    return isinstance(x, (list, tuple, np.ndarray, Series, Index, ObjArray, range, set)) or _is_iter(x)


def _is_boolvec(l):
    if len(l) == 0:
        return False
    for x in l:
        if type(x) is not bool:
            return False
    return True


def _to_index(x, n=None, name=None):
    if x is None:
        return RangeIndex(n)
    if isinstance(x, Index):
        return x
    if isinstance(x, range):
        return RangeIndex(x.start, x.stop, x.step, name=name)
    if isinstance(x, Series):
        return Index(x._v, name=x.name)
    if isinstance(x, np.ndarray):
        return Index(x.tolist(), name=name)
    if isinstance(x, ObjArray):
        return Index(x._a, name=name)
    if isinstance(x, (list, tuple)):
        if len(x) > 0 and isinstance(x[0], tuple) and name is None:
            return MultiIndex.from_tuples(x)
        return Index(list(x), name=name)
    return Index(list(x), name=name)


def _astype_vals(vals, dt, dn):
    """convert python list from dtype dt to dtype name dn"""
    if dn == dt:
        return list(vals)
    if dn == 'float64':
        if dt == 'str' or dt == 'object':
            out = []
            for x in vals:
                if x is None or x != x:
                    out.append(nan)
                else:
                    out.append(float(x))
            return out
        return [float(x) for x in vals]
    if dn == 'int64':
        if dt == 'float64':
            for x in vals:
                if x != x:
                    raise ValueError('Cannot convert non-finite values (NA or inf) to integer')
        return [int(x) for x in vals]
    if dn == 'str':
        return [nan if (x is None or x != x) else (x if isinstance(x, str) else str(x)) for x in vals]
    if dn == 'bool':
        return [bool(x) for x in vals]
    return list(vals)


def _pyfmt_scalar(x):
    return x


# ---------------------------------------------------------------- binary operations

_PYF = {
    '+': lambda a, b: a + b,
    '-': lambda a, b: a - b,
    '*': lambda a, b: a * b,
    '/': lambda a, b: a / b,
    '//': lambda a, b: a // b,
    '%': lambda a, b: a % b,
    '**': lambda a, b: a ** b,
    '==': lambda a, b: a == b,
    '!=': lambda a, b: a != b,
    '<': lambda a, b: a < b,
    '<=': lambda a, b: a <= b,
    '>': lambda a, b: a > b,
    '>=': lambda a, b: a >= b,
    '&': lambda a, b: a & b,
    '|': lambda a, b: a | b,
    '^': lambda a, b: a ^ b,
}
_CMP = ('==', '!=', '<', '<=', '>', '>=')


def _npop(op, a, b):
    if op == '+':
        return a + b
    if op == '-':
        return a - b
    if op == '*':
        return a * b
    if op == '/':
        return a / b
    if op == '//':
        return a // b
    if op == '%':
        return a % b
    if op == '**':
        return a ** b
    if op == '==':
        return a == b
    if op == '!=':
        return a != b
    if op == '<':
        return a < b
    if op == '<=':
        return a <= b
    if op == '>':
        return a > b
    if op == '>=':
        return a >= b
    if op == '&':
        return a & b
    if op == '|':
        return a | b
    if op == '^':
        return a ^ b
    raise ValueError(op)


def _isnum_scalar(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) or isinstance(x, bool)


def _calc(op, a, adt, b, bdt):
    """a: list; b: list (same length) or scalar. returns (list, dtype)"""
    bvec = isinstance(b, list)
    anum = adt == 'int64' or adt == 'float64' or adt == 'bool'
    if bvec:
        bnum = bdt == 'int64' or bdt == 'float64' or bdt == 'bool'
    else:
        bnum = isinstance(b, (int, float))
    if anum and bnum:
        A = np.array(a)
        B = np.array(b) if bvec else b
        if op == '/' and not bvec and b == 0:
            r = A * 1.0 / b if False else None
        if op in ('//', '%') and adt != 'float64' and bdt != 'float64' and (bvec or not isinstance(b, float)):
            # integer floor division / modulo by zero gives 0 in numpy; pandas gives inf / nan
            zero = False
            if bvec:
                for x in b:
                    if x == 0:
                        zero = True
                        break
            else:
                zero = b == 0
            if zero:
                A = A * 1.0
        r = _npop(op, A, B)
        return r.tolist(), _np_dt(r)
    f = _PYF[op]
    if op in _CMP:
        res = []
        if bvec:
            for x, y in zip(a, b):
                if isna_scalar(x) or isna_scalar(y):
                    res.append(op == '!=')
                else:
                    res.append(f(x, y))
        else:
            if isna_scalar(b):
                return [op == '!='] * len(a), 'bool'
            for x in a:
                if isna_scalar(x):
                    res.append(op == '!=')
                else:
                    res.append(f(x, b))
        return res, 'bool'
    res = []
    if bvec:
        for x, y in zip(a, b):
            if isna_scalar(x) or isna_scalar(y):
                res.append(nan)
            else:
                res.append(f(x, y))
    else:
        if isna_scalar(b):
            return [nan] * len(a), 'float64'
        for x in a:
            if isna_scalar(x):
                res.append(nan)
            else:
                res.append(f(x, b))
    return infer(res)


def _np_dt(r):
    d = str(r.dtype)
    if d.startswith('int') or d.startswith('uint'):
        return 'int64'
    if d.startswith('float'):
        return 'float64'
    if d.startswith('bool'):
        return 'bool'
    return 'object'


def _align_lists(ia, va, ib, vb):
    """align two label->value sets on the union of their labels; returns (index, la, lb)"""
    idx = ia.union(ib)
    pa = ia._mkpos()
    pb = ib._mkpos()
    la = [va[pa[k]] if k in pa else nan for k in idx._v]
    lb = [vb[pb[k]] if k in pb else nan for k in idx._v]
    return idx, la, lb


class _Loc:
    def __init__(self, obj, kind):
        self.obj = obj
        self.kind = kind

    def __getitem__(self, key):
        return self.obj._getitem_ix(key, self.kind)

    def __setitem__(self, key, value):
        self.obj._setitem_ix(key, value, self.kind)


class _At:
    def __init__(self, obj, kind):
        self.obj = obj
        self.kind = kind

    def __getitem__(self, key):
        return self.obj._getitem_ix(key, self.kind)

    def __setitem__(self, key, value):
        self.obj._setitem_ix(key, value, self.kind)


def _positions(idx, key, kind, n):
    """resolve one axis indexer to (positions list, is_scalar)"""
    if isinstance(key, slice):
        if kind == 'iloc':
            return list(range(n))[key], False
        st = key.start
        sp = key.stop
        if (st is not None and not isinstance(st, int)) or (sp is not None and not isinstance(sp, int)) or idx._dt != 'int64' or True:
            if kind == 'loc':
                lo = idx._bound(st, False) if st is not None else None
                hi = idx._bound(sp, True) if sp is not None else None
                if st is not None and sp is not None and idx._dt in ('int64', 'float64', 'str', 'object') and idx.is_monotonic_increasing is False and st in idx._mkpos() and sp in idx._mkpos():
                    lo = idx._locs(st)[0]
                    hi = idx._locs(sp)[-1] + 1
                return list(range(n))[slice(lo, hi, key.step)], False
        return list(range(n))[key], False
    if kind == 'iloc':
        if isinstance(key, (int,)) and not isinstance(key, bool):
            if key < -n or key >= n:
                raise IndexError('single positional indexer is out-of-bounds')
            return [key if key >= 0 else key + n], True
        if callable(key):
            return _positions(idx, key(None), kind, n)
        kl = _lst(key) if _is_listlike(key) else key
        if _is_boolvec(kl):
            return [i for i in range(n) if kl[i]], False
        return [(k if k >= 0 else k + n) for k in kl], False
    # loc
    if callable(key) and not isinstance(key, (str, int, float, tuple)):
        return _positions(idx, key(None), kind, n)
    if _is_listlike(key):
        kl = _lst(key)
        if isinstance(key, (Series,)) and key._dt == 'bool' or _is_boolvec(kl):
            if len(kl) != n:
                raise IndexError('Boolean index has wrong length: %d instead of %d' % (len(kl), n))
            return [i for i in range(n) if kl[i]], False
        if isinstance(key, tuple) and idx.nlevels > 1:
            ps = idx._locs(key)
            if not ps:
                k = len(key)
                ps = [i for i in range(n) if idx._v[i][:k] == key]
                if not ps:
                    raise KeyError(key)
                return ps, False
            return ps, len(ps) == 1 and True
        out = []
        pos = idx._mkpos()
        missing = []
        for k in kl:
            if k in pos:
                ps = idx._locs(k) if len(pos) != n else [pos[k]]
                out.extend(ps)
            else:
                missing.append(k)
        if missing:
            raise KeyError('%s not in index' % (missing,))
        return out, False
    ps = idx._locs(key)
    if len(ps) == 0:
        if idx.nlevels > 1 and not isinstance(key, tuple):
            ps = [i for i in range(n) if idx._v[i][0] == key]
            if ps:
                return ps, False
        raise KeyError(key)
    return ps, len(ps) == 1


class Series:
    def __init__(self, data=None, index=None, dtype=None, name=None, copy=False, fastpath=False, _dt=None):
        if _dt is not None:
            self._v = data
            self._idx = index
            self.name = name
            self._dt = _dt
            return
        if isinstance(data, Series):
            if name is None:
                name = data.name
            if index is None:
                index = data._idx
                data = list(data._v)
                dt0 = None
            else:
                ix = _to_index(index)
                p = data._idx._mkpos()
                data = [data._v[p[k]] if k in p else nan for k in ix._v]
                index = ix
        elif isinstance(data, dict):
            keys = list(data.keys())
            vals = list(data.values())
            if index is not None:
                ix = _to_index(index)
                data = [data.get(k, nan) for k in ix._v]
                index = ix
            else:
                index = Index(keys)
                data = vals
        elif data is None:
            data = []
        elif isinstance(data, np.ndarray):
            data = data.tolist()
        elif isinstance(data, ObjArray):
            data = list(data._a)
        elif isinstance(data, Index):
            data = list(data._v)
        elif isinstance(data, (list, tuple)):
            data = list(data)
        elif not _is_listlike(data):
            n = len(_to_index(index)) if index is not None else 1
            data = [data] * n
        else:
            data = list(data)
        if index is None:
            index = RangeIndex(len(data))
        else:
            index = _to_index(index)
            if len(index) != len(data):
                if len(data) == 1 and len(index) > 0:
                    data = data * len(index)
                else:
                    raise ValueError('Length of values (%d) does not match length of index (%d)' % (len(data), len(index)))
        dn = norm_dtype(dtype)
        data, dt = infer(data)
        if dn is not None and dn != dt:
            if len(data) == 0:
                dt = dn
            else:
                data = _astype_vals(data, dt, dn)
                dt = dn
        elif dn is None and len(data) == 0 and dtype is None:
            dt = 'object'
        self._v = data
        self._idx = index
        self.name = name
        self._dt = dt

    # ------------------------------------------------------------ basic properties
    @property
    def index(self):
        return self._idx

    @index.setter
    def index(self, value):
        ix = _to_index(value, len(self._v))
        if len(ix) != len(self._v):
            raise ValueError('Length mismatch: Expected axis has %d elements, new values have %d elements' % (len(self._v), len(ix)))
        self._idx = ix

    @property
    def dtype(self):
        return dtype_of(self._dt)

    @property
    def dtypes(self):
        return dtype_of(self._dt)

    @property
    def values(self):
        return to_np(self._v, self._dt)

    @property
    def shape(self):
        return (len(self._v),)

    @property
    def size(self):
        return len(self._v)

    @property
    def ndim(self):
        return 1

    @property
    def empty(self):
        return len(self._v) == 0

    @property
    def T(self):
        return self

    @property
    def hasnans(self):
        for x in self._v:
            if x is None or x != x:
                return True
        return False

    @property
    def is_unique(self):
        return len(set(self._v)) == len(self._v)

    @property
    def is_monotonic_increasing(self):
        v = self._v
        for i in range(1, len(v)):
            if v[i] < v[i - 1]:
                return False
        return True

    @property
    def is_monotonic_decreasing(self):
        v = self._v
        for i in range(1, len(v)):
            if v[i] > v[i - 1]:
                return False
        return True

    @property
    def loc(self):
        return _Loc(self, 'loc')

    @property
    def iloc(self):
        return _Loc(self, 'iloc')

    @property
    def at(self):
        return _At(self, 'loc')

    @property
    def iat(self):
        return _At(self, 'iloc')

    @property
    def str(self):
        from .strs import StringMethods
        return StringMethods(self)

    @property
    def dt(self):
        from .strs import DatetimeMethods
        return DatetimeMethods(self)

    @property
    def axes(self):
        return [self._idx]

    def _new(self, vals, dt=None, index=None, name=-1):
        if index is None:
            index = self._idx
        if name == -1:
            name = self.name
        if dt is None:
            vals, dt = infer(vals)
        return Series(vals, index, name=name, _dt=dt)

    # ------------------------------------------------------------ conversion
    def __len__(self):
        return len(self._v)

    def __iter__(self):
        return iter(self._v)

    def __contains__(self, key):
        return key in self._idx

    def __bool__(self):
        raise ValueError('The truth value of a Series is ambiguous. Use a.empty, a.bool(), a.item(), a.any() or a.all().')

    def __array__(self, dtype=None):
        return np.array(self._v)

    def tolist(self):
        return list(self._v)

    def to_list(self):
        return list(self._v)

    def to_numpy(self, dtype=None, copy=False, na_value=None):
        r = self.values
        if dtype is not None and isinstance(r, np.ndarray):
            return r.astype(dtype)
        return r

    def to_dict(self, into=dict):
        return dict(zip(self._idx._v, self._v))

    def to_frame(self, name=None):
        nm = name if name is not None else (self.name if self.name is not None else 0)
        return DataFrame._fast([list(self._v)], [self._dt], Index([nm]), self._idx)

    def items(self):
        return zip(self._idx._v, self._v)

    iteritems = items

    def keys(self):
        return self._idx

    def copy(self, deep=True):
        return Series(list(self._v), self._idx, name=self.name, _dt=self._dt)

    def item(self):
        if len(self._v) != 1:
            raise ValueError('can only convert an array of size 1 to a Python scalar')
        return self._v[0]

    def astype(self, dtype, copy=True, errors='raise'):
        dn = norm_dtype(dtype)
        if dn == self._dt:
            return self.copy()
        if dn == 'int64' and self._dt == 'str':
            return self._new([int(x) for x in self._v], 'int64')
        return self._new(_astype_vals(self._v, self._dt, dn), dn)

    def infer_objects(self):
        v, dt = infer(self._v)
        return self._new(v, dt)

    def convert_dtypes(self, *a, **k):
        return self.copy()

    def to_string(self, *a, **k):
        return _fmt.series_repr(self)

    def __repr__(self):
        return _fmt.series_repr(self)

    __str__ = __repr__

    def to_csv(self, path_or_buf=None, **kw):
        return self.to_frame().to_csv(path_or_buf, **kw)

    def to_json(self, *a, **k):
        import json
        return json.dumps(self.to_dict())

    # ------------------------------------------------------------ selection
    def __getitem__(self, key):
        v = self._v
        if isinstance(key, slice):
            n = len(v)
            st, sp = key.start, key.stop
            if (st is not None and not isinstance(st, int)) or (sp is not None and not isinstance(sp, int)):
                return self._getitem_ix(key, 'loc')
            pos = list(range(n))[key]
            return self._take(pos)
        if _is_listlike(key):
            kl = _lst(key)
            if _is_boolvec(kl):
                if isinstance(key, Series) and not key._idx.equals(self._idx):
                    p = key._idx._mkpos()
                    kl = [key._v[p[k]] if k in p else False for k in self._idx._v]
                return self._take([i for i in range(len(v)) if kl[i]])
            return self._getitem_ix(list(kl), 'loc')
        if callable(key):
            return self[key(self)]
        pos = self._idx._mkpos()
        if key in pos:
            ps = self._idx._locs(key)
            if len(ps) == 1:
                return v[ps[0]]
            return self._take(ps)
        if isinstance(key, int) and self._idx._dt != 'int64' and self._idx.nlevels == 1 and -len(v) <= key < len(v):
            return v[key]
        if self._idx.nlevels > 1:
            ps = [i for i in range(len(v)) if self._idx._v[i][0] == key]
            if ps:
                return self._take(ps, drop_first=True)
        raise KeyError(key)

    def _take(self, pos, drop_first=False):
        v = self._v
        iv = self._idx._v
        nix = self._idx._new([iv[i] for i in pos], self._idx.name)
        if drop_first:
            nix = nix.droplevel(0)
        return Series([v[i] for i in pos], nix, name=self.name, _dt=self._dt)

    def _getitem_ix(self, key, kind):
        n = len(self._v)
        if isinstance(key, tuple) and len(key) == 1:
            key = key[0]
        ps, scalar = _positions(self._idx, key, kind, n)
        if scalar:
            return self._v[ps[0]]
        r = self._take(ps)
        if kind == 'loc' and self._idx.nlevels > 1 and not _is_listlike(key) or (isinstance(key, tuple) and self._idx.nlevels > len(key)):
            k = len(key) if isinstance(key, tuple) else 1
            if k < self._idx.nlevels:
                r._idx = r._idx.droplevel(list(range(k))) if k > 1 else r._idx.droplevel(0)
        return r

    def _setitem_ix(self, key, value, kind):
        n = len(self._v)
        if kind == 'loc' and not _is_listlike(key) and not isinstance(key, slice) and key not in self._idx:
            # enlarge
            self._v = self._v + [value]
            self._idx = self._idx.append(Index([key])) if self._idx.nlevels == 1 else self._idx
            self._reinfer()
            return
        ps, scalar = _positions(self._idx, key, kind, n)
        self._assign(ps, value)

    def _assign(self, ps, value):
        v = list(self._v)
        if _is_listlike(value):
            if isinstance(value, Series):
                p = value._idx._mkpos()
                vl = [value._v[p[self._idx._v[i]]] if self._idx._v[i] in p else nan for i in ps]
            else:
                vl = _lst(value)
            if len(vl) != len(ps):
                raise ValueError('Length of values (%d) does not match length of index (%d)' % (len(vl), len(ps)))
            for i, x in zip(ps, vl):
                v[i] = x
        else:
            for i in ps:
                v[i] = value
        self._v = v
        self._reinfer()

    def _reinfer(self):
        dt0 = self._dt
        v, dt = infer(self._v)
        if dt0 == 'float64' and dt == 'object' and len(v) > 0:
            pass
        self._v = v
        self._dt = dt

    def __setitem__(self, key, value):
        v = self._v
        if isinstance(key, slice):
            st, sp = key.start, key.stop
            if (st is not None and not isinstance(st, int)) or (sp is not None and not isinstance(sp, int)):
                self._setitem_ix(key, value, 'loc')
            else:
                self._assign(list(range(len(v)))[key], value)
            return
        if _is_listlike(key):
            kl = _lst(key)
            if _is_boolvec(kl):
                self._assign([i for i in range(len(v)) if kl[i]], value)
                return
            self._setitem_ix(list(kl), value, 'loc')
            return
        if key in self._idx:
            self._setitem_ix(key, value, 'loc')
        elif isinstance(key, int) and self._idx._dt != 'int64' and -len(v) <= key < len(v):
            self._assign([key if key >= 0 else key + len(v)], value)
        else:
            self._setitem_ix(key, value, 'loc')

    def __delitem__(self, key):
        r = self.drop(key)
        self._v, self._idx, self._dt = r._v, r._idx, r._dt

    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    def pop(self, key):
        r = self[key]
        self.__delitem__(key)
        return r

    def head(self, n=5):
        if n >= 0:
            return self._take(list(range(min(n, len(self._v)))))
        return self._take(list(range(max(len(self._v) + n, 0))))

    def tail(self, n=5):
        L = len(self._v)
        if n >= 0:
            return self._take(list(range(max(L - n, 0), L)))
        return self._take(list(range(min(-n, L), L)))

    def take(self, indices, axis=0):
        return self._take([i if i >= 0 else i + len(self._v) for i in _lst(indices)])

    def sample(self, n=None, frac=None, replace=False, random_state=None, **kw):
        import random
        if random_state is not None:
            random.seed(random_state)
        L = len(self._v)
        if n is None:
            n = int(round(frac * L))
        if replace:
            pos = [random.randrange(L) for _ in range(n)]
        else:
            pos = random.sample(list(range(L)), n)
        return self._take(pos)

    # ------------------------------------------------------------ arithmetic
    def _binop(self, other, op, rev=False):
        name = self.name
        if isinstance(other, Series):
            if other.name != self.name:
                name = None
            if self._idx is other._idx or self._idx._v == other._idx._v:
                a, b = self._v, other._v
                adt, bdt = self._dt, other._dt
                idx = self._idx
            else:
                idx, a, b = _align_lists(self._idx, self._v, other._idx, other._v)
                adt = 'float64' if self._dt in ('int64', 'bool') else self._dt
                bdt = 'float64' if other._dt in ('int64', 'bool') else other._dt
                if adt == 'object' or adt == 'str':
                    pass
                if len(a) != len(self._v) or True:
                    # missing labels introduced NaN
                    if self._dt in ('int64', 'bool'):
                        a = [float(x) for x in a]
                    if other._dt in ('int64', 'bool'):
                        b = [float(x) for x in b]
        elif isinstance(other, DataFrame):
            return other._binop(self, op, not rev)
        elif _is_listlike(other):
            b = _lst(other)
            if len(b) != len(self._v):
                raise ValueError('Lengths must match to compare' if op in _CMP else 'Unable to coerce to Series, length must be %d: given %d' % (len(self._v), len(b)))
            b, bdt = infer(list(b))
            a, adt = self._v, self._dt
            idx = self._idx
        else:
            a, adt = self._v, self._dt
            b, bdt = other, None
            idx = self._idx
            if other is NA or other is None:
                b = nan
        if rev:
            if isinstance(b, list):
                vals, dt = _calc(op, b, bdt, a, adt)
            else:
                vals, dt = self._rscalar(op, b)
        else:
            vals, dt = _calc(op, a, adt, b, bdt)
        return Series(vals, idx, name=name, _dt=dt)

    def _rscalar(self, op, b):
        a = self._v
        adt = self._dt
        if (adt == 'int64' or adt == 'float64' or adt == 'bool') and isinstance(b, (int, float)):
            B = np.array(a)
            r = _npop(op, b, B)
            return r.tolist(), _np_dt(r)
        f = _PYF[op]
        res = []
        for x in a:
            if isna_scalar(x):
                res.append(op == '!=' if op in _CMP else nan)
            else:
                res.append(f(b, x))
        if op in _CMP:
            return res, 'bool'
        return infer(res)

    def __add__(self, o):
        return self._binop(o, '+')

    def __radd__(self, o):
        return self._binop(o, '+', True)

    def __sub__(self, o):
        return self._binop(o, '-')

    def __rsub__(self, o):
        return self._binop(o, '-', True)

    def __mul__(self, o):
        return self._binop(o, '*')

    def __rmul__(self, o):
        return self._binop(o, '*', True)

    def __truediv__(self, o):
        return self._binop(o, '/')

    def __rtruediv__(self, o):
        return self._binop(o, '/', True)

    def __floordiv__(self, o):
        return self._binop(o, '//')

    def __rfloordiv__(self, o):
        return self._binop(o, '//', True)

    def __mod__(self, o):
        return self._binop(o, '%')

    def __rmod__(self, o):
        return self._binop(o, '%', True)

    def __pow__(self, o):
        return self._binop(o, '**')

    def __rpow__(self, o):
        return self._binop(o, '**', True)

    def __eq__(self, o):
        return self._binop(o, '==')

    def __ne__(self, o):
        return self._binop(o, '!=')

    def __lt__(self, o):
        return self._binop(o, '<')

    def __le__(self, o):
        return self._binop(o, '<=')

    def __gt__(self, o):
        return self._binop(o, '>')

    def __ge__(self, o):
        return self._binop(o, '>=')

    def __and__(self, o):
        return self._binop(o, '&')

    def __rand__(self, o):
        return self._binop(o, '&', True)

    def __or__(self, o):
        return self._binop(o, '|')

    def __ror__(self, o):
        return self._binop(o, '|', True)

    def __xor__(self, o):
        return self._binop(o, '^')

    def __hash__(self):
        raise TypeError("unhashable type: 'Series'")

    def __neg__(self):
        if self._dt == 'bool':
            return self._new([not x for x in self._v], 'bool')
        r = -np.array(self._v)
        return self._new(r.tolist(), self._dt)

    def __pos__(self):
        return self.copy()

    def __invert__(self):
        if self._dt == 'bool':
            return self._new([not x for x in self._v], 'bool')
        return self._new([~x for x in self._v], self._dt)

    def __abs__(self):
        return self.abs()

    def abs(self):
        return self._new(np.abs(np.array(self._v)).tolist(), self._dt)

    def add(self, o, fill_value=None):
        return self._binop2(o, '+', fill_value)

    def sub(self, o, fill_value=None):
        return self._binop2(o, '-', fill_value)

    def mul(self, o, fill_value=None):
        return self._binop2(o, '*', fill_value)

    def div(self, o, fill_value=None):
        return self._binop2(o, '/', fill_value)

    truediv = div

    def floordiv(self, o, fill_value=None):
        return self._binop2(o, '//', fill_value)

    def mod(self, o, fill_value=None):
        return self._binop2(o, '%', fill_value)

    def pow(self, o, fill_value=None):
        return self._binop2(o, '**', fill_value)

    def radd(self, o, fill_value=None):
        return self._binop(o, '+', True)

    def rsub(self, o, fill_value=None):
        return self._binop(o, '-', True)

    def rmul(self, o, fill_value=None):
        return self._binop(o, '*', True)

    def rdiv(self, o, fill_value=None):
        return self._binop(o, '/', True)

    rtruediv = rdiv

    def eq(self, o):
        return self._binop(o, '==')

    def ne(self, o):
        return self._binop(o, '!=')

    def lt(self, o):
        return self._binop(o, '<')

    def le(self, o):
        return self._binop(o, '<=')

    def gt(self, o):
        return self._binop(o, '>')

    def ge(self, o):
        return self._binop(o, '>=')

    def _binop2(self, o, op, fill_value):
        if fill_value is not None:
            a = self.fillna(fill_value)
            if isinstance(o, Series):
                if not a._idx.equals(o._idx):
                    idx = a._idx.union(o._idx)
                    a = a.reindex(idx).fillna(fill_value)
                    o = o.reindex(idx)
                o = o.fillna(fill_value)
            return a._binop(o, op)
        return self._binop(o, op)

    def dot(self, other):
        return float(np.dot(np.array(self._v), np.array(_lst(other))))

    def __matmul__(self, other):
        return self.dot(other)

    def round(self, decimals=0):
        if self._dt == 'int64':
            return self.copy()
        if self._dt != 'float64':
            raise TypeError('cannot round non-numeric Series')
        return self._new(np.round(np.array(self._v), decimals).tolist(), 'float64')

    def clip(self, lower=None, upper=None):
        if self._dt not in _NUMDT:
            raise TypeError('clip needs numeric data')
        a = np.array(self._v)
        if lower is not None:
            a = np.maximum(a, lower)
        if upper is not None:
            a = np.minimum(a, upper)
        r = a.tolist()
        if self._dt == 'float64':
            # keep nan
            r = [x if y == y else y for x, y in zip(r, self._v)]
        return self._new(r, _np_dt(a))

    def between(self, left, right, inclusive='both'):
        v = self._v
        res = []
        for x in v:
            if x != x:
                res.append(False)
                continue
            lo = x >= left if inclusive in ('both', 'left') else x > left
            hi = x <= right if inclusive in ('both', 'right') else x < right
            res.append(lo and hi)
        return self._new(res, 'bool')

    def isin(self, values):
        if isinstance(values, dict):
            values = list(values.keys())
        s = set(_lst(values)) if _is_listlike(values) else set([values])
        return self._new([x in s for x in self._v], 'bool')

    def isna(self):
        return self._new([x is None or x != x or x is NA for x in self._v], 'bool')

    isnull = isna

    def notna(self):
        return self._new([not (x is None or x != x or x is NA) for x in self._v], 'bool')

    notnull = notna

    def where(self, cond, other=nan):
        c = _lst(cond)
        if isinstance(other, Series):
            o = _lst(other.reindex(self._idx))
        elif _is_listlike(other):
            o = _lst(other)
        else:
            o = [other] * len(self._v)
        return self._new([x if k else y for x, k, y in zip(self._v, c, o)])

    def mask(self, cond, other=nan):
        c = _lst(cond)
        return self.where([not k for k in c], other)

    def combine_first(self, other):
        idx = self._idx.union(other._idx)
        a = self.reindex(idx)
        b = other.reindex(idx)
        return a._new([y if (x != x) else x for x, y in zip(a._v, b._v)])

    def fillna(self, value=None, method=None, limit=None, inplace=False, downcast=None):
        if method is not None:
            return self.ffill() if method in ('ffill', 'pad') else self.bfill()
        if isinstance(value, (dict, Series)):
            d = value if isinstance(value, dict) else value.to_dict()
            res = [d.get(i, x) if (x is None or x != x) else x for i, x in zip(self._idx._v, self._v)]
        else:
            if self._dt in ('int64', 'bool'):
                return self.copy()
            res = [value if (x is None or x != x) else x for x in self._v]
        r = self._new(res)
        if inplace:
            self._v, self._dt = r._v, r._dt
            return None
        return r

    def ffill(self, limit=None):
        res = []
        last = nan
        run = 0
        for x in self._v:
            if x is None or x != x:
                run += 1
                if limit is None or run <= limit:
                    res.append(last)
                else:
                    res.append(x)
            else:
                last = x
                run = 0
                res.append(x)
        return self._new(res)

    def bfill(self, limit=None):
        res = []
        last = nan
        run = 0
        for x in self._v[::-1]:
            if x is None or x != x:
                run += 1
                if limit is None or run <= limit:
                    res.append(last)
                else:
                    res.append(x)
            else:
                last = x
                run = 0
                res.append(x)
        return self._new(res[::-1])

    pad = ffill
    backfill = bfill

    def dropna(self, axis=0, inplace=False, how=None):
        pos = [i for i in range(len(self._v)) if not (self._v[i] is None or self._v[i] != self._v[i])]
        r = self._take(pos)
        if inplace:
            self._v, self._idx = r._v, r._idx
            return None
        return r

    def replace(self, to_replace=None, value=None, inplace=False, regex=False):
        if isinstance(to_replace, dict):
            m = to_replace
            res = [m[x] if x in m else x for x in self._v]
        elif _is_listlike(to_replace):
            tl = _lst(to_replace)
            if _is_listlike(value):
                m = dict(zip(tl, _lst(value)))
                res = [m[x] if x in m else x for x in self._v]
            else:
                s = set(tl)
                res = [value if x in s else x for x in self._v]
        else:
            if regex and isinstance(to_replace, str):
                import re
                rx = re.compile(to_replace)
                res = [rx.sub(value, x) if isinstance(x, str) else x for x in self._v]
            elif isinstance(to_replace, float) and to_replace != to_replace:
                res = [value if x != x else x for x in self._v]
            else:
                res = [value if (x == to_replace and type(x) is not bool or (type(x) is bool and x is to_replace)) else x for x in self._v]
        r = self._new(res)
        if inplace:
            self._v, self._dt = r._v, r._dt
            return None
        return r

    def rename(self, index=None, **kw):
        if index is None or callable(index) is False and not isinstance(index, dict):
            r = self.copy()
            if index is not None:
                r.name = index
            return r
        r = self.copy()
        if callable(index):
            r._idx = Index([index(x) for x in self._idx._v], name=self._idx.name)
        else:
            r._idx = Index([index.get(x, x) for x in self._idx._v], name=self._idx.name)
        return r

    def rename_axis(self, mapper=None, index=None, **kw):
        r = self.copy()
        nm = index if index is not None else mapper
        r._idx = self._idx.copy()
        r._idx.rename(nm, inplace=True)
        return r

    def set_axis(self, labels, axis=0, copy=True):
        r = self.copy()
        r.index = labels
        return r

    def reset_index(self, level=None, drop=False, name=None, inplace=False, allow_duplicates=False):
        if drop:
            r = self.copy()
            r._idx = RangeIndex(len(self._v))
            return r
        nm = name if name is not None else (self.name if self.name is not None else 0)
        idx = self._idx
        cols = {}
        if idx.nlevels == 1:
            cols[idx.name if idx.name is not None else 'index'] = list(idx._v)
        else:
            for k in range(idx.nlevels):
                cols[idx.names[k] if idx.names[k] is not None else 'level_%d' % k] = [t[k] for t in idx._v]
        cols[nm] = list(self._v)
        return DataFrame(cols)

    def reindex(self, index=None, fill_value=nan, method=None, **kw):
        ix = _to_index(index)
        p = self._idx._mkpos()
        if method in ('ffill', 'pad'):
            res = []
            last = nan
            src = self.reindex(ix.__class__(sortkey_list(list(set(ix._v) | set(self._idx._v)))))
            return src.ffill().reindex(ix)
        res = [self._v[p[k]] if k in p else fill_value for k in ix._v]
        if self._dt == 'object' or self._dt == 'str':
            return Series(res, ix, name=self.name)
        return Series(res, ix, name=self.name) if len(res) else Series([], ix, name=self.name, dtype=self._dt)

    def align(self, other, join='outer'):
        idx = self._idx.union(other._idx)
        return self.reindex(idx), other.reindex(idx)

    def drop(self, labels=None, axis=0, index=None, columns=None, level=None, inplace=False, errors='raise'):
        if index is not None:
            labels = index
        ls = _lst(labels) if _is_listlike(labels) else [labels]
        s = set(ls)
        if errors == 'raise':
            for k in ls:
                if k not in self._idx:
                    raise KeyError('%s not found in axis' % ([k],))
        pos = [i for i in range(len(self._v)) if self._idx._v[i] not in s]
        r = self._take(pos)
        if inplace:
            self._v, self._idx = r._v, r._idx
            return None
        return r

    # ------------------------------------------------------------ apply / map
    def apply(self, func, convert_dtype=True, args=(), by_row='compat', **kwargs):
        if isinstance(func, str):
            return getattr(self, func)(*args, **kwargs)
        res = [func(x, *args, **kwargs) for x in self._v]
        if len(res) > 0 and isinstance(res[0], Series):
            return DataFrame([r for r in res], index=self._idx)
        return self._new(res)

    def map(self, arg, na_action=None):
        if isinstance(arg, dict):
            d = arg
            res = [d.get(x, nan) if not (na_action == 'ignore' and isna_scalar(x)) else nan for x in self._v]
            if len(d) > 0:
                dv, ddt = infer(list(d.values()))
                if ddt == 'str':
                    return self._new([nan if (x != x) else x for x in res], 'str')
        elif isinstance(arg, Series):
            d = arg.to_dict()
            res = [d.get(x, nan) for x in self._v]
        else:
            if na_action == 'ignore':
                res = [nan if isna_scalar(x) else arg(x) for x in self._v]
            else:
                res = [arg(x) for x in self._v]
        return self._new(res)

    def transform(self, func, *a, **k):
        if isinstance(func, str):
            return getattr(self, func)(*a, **k)
        return self.apply(func)

    def pipe(self, func, *args, **kwargs):
        return func(self, *args, **kwargs)

    def agg(self, func=None, *args, **kwargs):
        if isinstance(func, str):
            return getattr(self, func)(*args, **kwargs)
        if callable(func):
            try:
                return func(self)
            except Exception:
                return self.apply(func)
        if isinstance(func, (list, tuple)):
            names = []
            vals = []
            for f in func:
                names.append(f if isinstance(f, str) else f.__name__)
                vals.append(self.agg(f))
            return Series(vals, Index(names), name=self.name)
        if isinstance(func, dict):
            names = list(func.keys())
            return Series([self.agg(func[k]) for k in names], Index(names), name=self.name)
        raise ValueError('bad agg')

    aggregate = agg

    # ------------------------------------------------------------ statistics
    def _nn(self):
        """numpy array of non-missing numeric values"""
        dt = self._dt
        if dt == 'int64' or dt == 'bool':
            return np.array(self._v)
        if dt == 'float64':
            a = np.array(self._v)
            return a[a == a]
        raise TypeError('could not convert string to float: %r' % (self._v[0] if self._v else '',))

    def _nonna(self):
        return [x for x in self._v if not (x is None or x != x)]

    def count(self):
        if self._dt in ('int64', 'bool'):
            return len(self._v)
        return len(self._nonna())

    def sum(self, axis=None, skipna=True, numeric_only=False, min_count=0, **kw):
        dt = self._dt
        if dt == 'int64':
            return sum(self._v)
        if dt == 'bool':
            return sum(self._v)
        if dt == 'float64':
            if not skipna:
                return float(np.array(self._v).sum()) if len(self._v) else 0.0
            a = self._nn()
            if len(a) < min_count:
                return nan
            return float(a.sum()) if len(a) else 0.0
        nn = self._nonna()
        if len(nn) == 0:
            return 0
        t = nn[0]
        for x in nn[1:]:
            t = t + x
        return t

    def prod(self, axis=None, skipna=True, **kw):
        a = self._nn()
        if len(a) == 0:
            return 1
        r = a.prod()
        return r

    product = prod

    def mean(self, axis=None, skipna=True, numeric_only=False, **kw):
        a = self._nn() if skipna else np.array(self._v)
        n = len(a)
        if n == 0:
            return nan
        return float(a.sum()) / n

    def median(self, axis=None, skipna=True, **kw):
        a = self._nn()
        if len(a) == 0:
            return nan
        return float(np.median(a))

    def var(self, axis=None, skipna=True, ddof=1, **kw):
        a = self._nn()
        n = len(a)
        if n - ddof <= 0:
            return nan
        m = float(a.sum()) / n
        d = a - m
        return float((d * d).sum()) / (n - ddof)

    def std(self, axis=None, skipna=True, ddof=1, **kw):
        v = self.var(ddof=ddof)
        return math.sqrt(v) if v == v else nan

    def sem(self, axis=None, skipna=True, ddof=1, **kw):
        s = self.std(ddof=ddof)
        n = self.count()
        return s / math.sqrt(n) if n else nan

    def skew(self, **kw):
        a = self._nn()
        n = len(a)
        if n < 3:
            return nan
        m = float(a.sum()) / n
        d = a - m
        m2 = float((d * d).sum()) / n
        m3 = float((d * d * d).sum()) / n
        if m2 == 0:
            return 0.0
        g = m3 / m2 ** 1.5
        return g * math.sqrt(n * (n - 1)) / (n - 2)

    def kurt(self, **kw):
        a = self._nn()
        n = len(a)
        if n < 4:
            return nan
        m = float(a.sum()) / n
        d = a - m
        m2 = float((d * d).sum()) / n
        m4 = float((d * d * d * d).sum()) / n
        if m2 == 0:
            return 0.0
        g = m4 / (m2 * m2) - 3
        return ((n - 1) / ((n - 2) * (n - 3))) * ((n + 1) * g + 6)

    kurtosis = kurt

    def min(self, axis=None, skipna=True, numeric_only=False, **kw):
        dt = self._dt
        if dt == 'int64' or dt == 'bool':
            return min(self._v) if self._v else nan
        if dt == 'float64':
            a = self._nn() if skipna else np.array(self._v)
            if len(a) == 0:
                return nan
            if not skipna and a.sum() != a.sum():
                return nan
            return float(a.min())
        nn = self._nonna()
        if len(nn) == 0:
            return nan
        return min(nn)

    def max(self, axis=None, skipna=True, numeric_only=False, **kw):
        dt = self._dt
        if dt == 'int64' or dt == 'bool':
            return max(self._v) if self._v else nan
        if dt == 'float64':
            a = self._nn() if skipna else np.array(self._v)
            if len(a) == 0:
                return nan
            if not skipna and a.sum() != a.sum():
                return nan
            return float(a.max())
        nn = self._nonna()
        if len(nn) == 0:
            return nan
        return max(nn)

    def ptp(self):
        return self.max() - self.min()

    def quantile(self, q=0.5, interpolation='linear'):
        a = sorted(self._nn().tolist())
        n = len(a)
        if isinstance(q, (list, tuple, np.ndarray)):
            ql = list(q)
            return Series([_quant(a, x) for x in ql], Index([float(x) for x in ql]), name=self.name)
        if n == 0:
            return nan
        return _quant(a, q)

    def mode(self, dropna=True):
        cnt = {}
        for x in self._v:
            if dropna and (x is None or x != x):
                continue
            cnt[x] = cnt.get(x, 0) + 1
        if not cnt:
            return Series([], dtype=self._dt)
        m = max(cnt.values())
        vals = sortkey_list([k for k, c in cnt.items() if c == m])
        return Series(vals, dtype=self._dt)

    def nunique(self, dropna=True):
        s = set(self._v)
        n = len(set([x for x in self._v if not (x is None or x != x)]))
        if not dropna and self.hasnans:
            n += 1
        return n

    def unique(self):
        seen = {}
        out = []
        for x in self._v:
            if x not in seen:
                seen[x] = 1
                out.append(x)
        return to_np(out, self._dt)

    def duplicated(self, keep='first'):
        seen = {}
        res = []
        v = self._v
        if keep == 'first':
            for x in v:
                res.append(x in seen)
                seen[x] = 1
        elif keep == 'last':
            for x in v[::-1]:
                res.append(x in seen)
                seen[x] = 1
            res = res[::-1]
        else:
            cnt = {}
            for x in v:
                cnt[x] = cnt.get(x, 0) + 1
            res = [cnt[x] > 1 for x in v]
        return self._new(res, 'bool')

    def drop_duplicates(self, keep='first', inplace=False, ignore_index=False):
        d = self.duplicated(keep)._v
        r = self._take([i for i in range(len(d)) if not d[i]])
        if ignore_index:
            r._idx = RangeIndex(len(r._v))
        return r

    def value_counts(self, normalize=False, sort=True, ascending=False, bins=None, dropna=True):
        cnt = {}
        order = []
        nnan = 0
        nanpos = -1
        for x in self._v:
            if x is None or x != x:
                if dropna:
                    continue
                if nnan == 0:
                    nanpos = len(order)
                    order.append(nan)
                nnan += 1
                continue
            if x in cnt:
                cnt[x] += 1
            else:
                cnt[x] = 1
                order.append(x)
        items = [(k, cnt[k] if k == k else nnan) for k in order]
        if sort:
            items = sorted(items, key=lambda kv: kv[1], reverse=not ascending)
        total = sum(cnt.values())
        keys = [k for k, c in items]
        if normalize:
            vals = [c / total for k, c in items]
            nm = 'proportion'
        else:
            vals = [c for k, c in items]
            nm = 'count'
        ix = Index(keys, name=self.name)
        if self._dt in ('str', 'object', 'bool', 'int64', 'float64'):
            ix._dt = self._dt if len(keys) else ix._dt
        return Series(vals, ix, name=nm, _dt='float64' if normalize else 'int64')

    def idxmax(self, skipna=True):
        a = self._nonna_pairs()
        if not a:
            return nan
        best = a[0]
        for p in a[1:]:
            if p[1] > best[1]:
                best = p
        return best[0]

    def idxmin(self, skipna=True):
        a = self._nonna_pairs()
        if not a:
            return nan
        best = a[0]
        for p in a[1:]:
            if p[1] < best[1]:
                best = p
        return best[0]

    def _nonna_pairs(self):
        return [(i, x) for i, x in zip(self._idx._v, self._v) if not (x is None or x != x)]

    def argmax(self, skipna=True):
        best = -1
        bv = None
        for i, x in enumerate(self._v):
            if x != x:
                continue
            if best < 0 or x > bv:
                best, bv = i, x
        return best

    def argmin(self, skipna=True):
        best = -1
        bv = None
        for i, x in enumerate(self._v):
            if x != x:
                continue
            if best < 0 or x < bv:
                best, bv = i, x
        return best

    def any(self, axis=None, skipna=True, **kw):
        for x in self._v:
            if x == x and x:
                return True
        return False

    def all(self, axis=None, skipna=True, **kw):
        for x in self._v:
            if x == x and not x:
                return False
        return True

    def equals(self, other):
        return isinstance(other, Series) and self._v == other._v and self._idx._v == other._idx._v

    def corr(self, other, method='pearson', min_periods=None):
        a, b = _pairs(self, other)
        if len(a) < 2:
            return nan
        if method == 'spearman':
            a = _rank_list(a)
            b = _rank_list(b)
        A = np.array(a)
        B = np.array(b)
        da = A - A.mean()
        db = B - B.mean()
        den = math.sqrt(float((da * da).sum()) * float((db * db).sum()))
        if den == 0:
            return nan
        return float((da * db).sum()) / den

    def cov(self, other, min_periods=None, ddof=1):
        a, b = _pairs(self, other)
        n = len(a)
        if n < 2:
            return nan
        A = np.array(a)
        B = np.array(b)
        return float(((A - A.mean()) * (B - B.mean())).sum()) / (n - ddof)

    def autocorr(self, lag=1):
        return self.corr(self.shift(lag))

    def describe(self, percentiles=None, include=None, exclude=None):
        if self._dt in _NUMDT and self._dt != 'bool':
            ps = percentiles if percentiles is not None else [0.25, 0.5, 0.75]
            nn = sorted(self._nn().tolist())
            labs = ['count', 'mean', 'std', 'min'] + [_pctlab(p) for p in ps] + ['max']
            vals = [float(len(nn)), self.mean(), self.std(), float(nn[0]) if nn else nan] + [_quant(nn, p) if nn else nan for p in ps] + [float(nn[-1]) if nn else nan]
            return Series(vals, Index(labs), name=self.name, _dt='float64')
        nn = self._nonna()
        cnt = {}
        for x in nn:
            cnt[x] = cnt.get(x, 0) + 1
        if cnt:
            top = None
            fr = 0
            for k, c in cnt.items():
                if c > fr:
                    top, fr = k, c
        else:
            top, fr = nan, nan
        return Series([len(nn), len(cnt), top, fr], Index(['count', 'unique', 'top', 'freq']), name=self.name, dtype='object')

    # ------------------------------------------------------------ cumulative / windows
    def cumsum(self, axis=None, skipna=True, **kw):
        return self._cum('sum')

    def cumprod(self, axis=None, skipna=True, **kw):
        return self._cum('prod')

    def cummax(self, axis=None, skipna=True, **kw):
        return self._cum('max')

    def cummin(self, axis=None, skipna=True, **kw):
        return self._cum('min')

    def _cum(self, kind):
        dt = self._dt
        if dt == 'int64' or dt == 'bool':
            a = np.array(self._v)
            if kind == 'sum':
                r = a.cumsum()
            elif kind == 'prod':
                r = np.cumprod(a)
            else:
                r = None
            if r is not None:
                return self._new(r.tolist(), 'int64')
        res = []
        acc = None
        for x in self._v:
            if x is None or x != x:
                res.append(nan)
                continue
            if acc is None:
                acc = x
            elif kind == 'sum':
                acc = acc + x
            elif kind == 'prod':
                acc = acc * x
            elif kind == 'max':
                acc = x if x > acc else acc
            else:
                acc = x if x < acc else acc
            res.append(acc)
        return self._new(res, dt if dt != 'bool' else 'int64')

    def diff(self, periods=1):
        v = self._v
        n = len(v)
        if self._dt not in _NUMDT:
            raise TypeError('unsupported operand')
        res = [nan] * n
        for i in range(n):
            j = i - periods
            if 0 <= j < n:
                res[i] = float(v[i] - v[j])
        return self._new(res, 'float64')

    def pct_change(self, periods=1, **kw):
        v = self._v
        n = len(v)
        res = [nan] * n
        for i in range(n):
            j = i - periods
            if 0 <= j < n and v[j] == v[j] and v[i] == v[i]:
                res[i] = v[i] / v[j] - 1 if v[j] != 0 else (math.inf if v[i] > 0 else nan)
        return self._new(res, 'float64')

    def shift(self, periods=1, freq=None, fill_value=nan):
        v = self._v
        n = len(v)
        if periods >= 0:
            res = [fill_value] * min(periods, n) + v[:max(n - periods, 0)]
        else:
            res = v[min(-periods, n):] + [fill_value] * min(-periods, n)
        if self._dt == 'bool' or self._dt == 'int64':
            if fill_value != fill_value:
                return self._new([float(x) if x == x and self._dt == 'int64' else x for x in res], 'float64' if self._dt == 'int64' else 'object')
        return self._new(res)

    def rank(self, method='average', ascending=True, na_option='keep', pct=False):
        v = self._v
        n = len(v)
        order = sorted([i for i in range(n) if v[i] == v[i]], key=lambda i: v[i], reverse=not ascending)
        ranks = [nan] * n
        i = 0
        m = len(order)
        while i < m:
            j = i
            while j + 1 < m and v[order[j + 1]] == v[order[i]]:
                j += 1
            for k in range(i, j + 1):
                if method == 'average':
                    ranks[order[k]] = (i + j) / 2.0 + 1
                elif method == 'min':
                    ranks[order[k]] = float(i + 1)
                elif method == 'max':
                    ranks[order[k]] = float(j + 1)
                elif method == 'dense':
                    ranks[order[k]] = 0.0
                else:
                    ranks[order[k]] = float(k + 1)
            i = j + 1
        if method == 'dense':
            r = 0
            prev = None
            for i in order:
                if prev is None or v[i] != prev:
                    r += 1
                    prev = v[i]
                ranks[i] = float(r)
        if pct:
            ranks = [x / m if x == x else x for x in ranks]
        return self._new(ranks, 'float64')

    def rolling(self, window, min_periods=None, center=False, **kw):
        from .window import Rolling
        return Rolling(self, window, min_periods, center)

    def expanding(self, min_periods=1, **kw):
        from .window import Expanding
        return Expanding(self, min_periods)

    def ewm(self, com=None, span=None, halflife=None, alpha=None, adjust=True, min_periods=0, **kw):
        from .window import EWM
        return EWM(self, com, span, halflife, alpha, adjust, min_periods)

    def groupby(self, by=None, level=None, as_index=True, sort=True, dropna=True, **kw):
        from .groupby import SeriesGroupBy
        return SeriesGroupBy(self, by, level, as_index, sort, dropna)

    # ------------------------------------------------------------ sorting
    def _order(self, ascending=True, na_position='last', key=None):
        v = self._v
        kv = v
        if key is not None:
            kv = key(self)._v
        n = len(v)
        good = [i for i in range(n) if not (kv[i] is None or kv[i] != kv[i])]
        bad = [i for i in range(n) if (kv[i] is None or kv[i] != kv[i])]
        good = sorted(good, key=lambda i: kv[i], reverse=not ascending)
        return good + bad if na_position == 'last' else bad + good

    def sort_values(self, axis=0, ascending=True, inplace=False, kind='quicksort', na_position='last', ignore_index=False, key=None):
        r = self._take(self._order(ascending, na_position, key))
        if ignore_index:
            r._idx = RangeIndex(len(r._v))
        if inplace:
            self._v, self._idx = r._v, r._idx
            return None
        return r

    def sort_index(self, axis=0, level=None, ascending=True, inplace=False, kind='quicksort', na_position='last', ignore_index=False, key=None):
        iv = self._idx._v
        order = sorted(range(len(iv)), key=lambda i: iv[i], reverse=not ascending)
        r = self._take(order)
        if ignore_index:
            r._idx = RangeIndex(len(r._v))
        if inplace:
            self._v, self._idx = r._v, r._idx
            return None
        return r

    def argsort(self, **kw):
        return self._new(self._order(), 'int64')

    def nlargest(self, n=5, keep='first'):
        o = self._order(False)
        o = [i for i in o if self._v[i] == self._v[i]][:n]
        return self._take(o)

    def nsmallest(self, n=5, keep='first'):
        o = self._order(True)
        o = [i for i in o if self._v[i] == self._v[i]][:n]
        return self._take(o)

    def reverse(self):
        return self._take(list(range(len(self._v) - 1, -1, -1)))

    def explode(self):
        idx = []
        vals = []
        for i, x in zip(self._idx._v, self._v):
            if isinstance(x, (list, tuple)):
                for y in x:
                    idx.append(i)
                    vals.append(y)
            else:
                idx.append(i)
                vals.append(x)
        return Series(vals, Index(idx, name=self._idx.name), name=self.name)

    def update(self, other):
        p = other._idx._mkpos()
        v = list(self._v)
        for i, k in enumerate(self._idx._v):
            if k in p and other._v[p[k]] == other._v[p[k]]:
                v[i] = other._v[p[k]]
        self._v = v
        self._reinfer()

    def append(self, to_append, ignore_index=False):
        from .reshape import concat
        return concat([self, to_append], ignore_index=ignore_index)

    def squeeze(self):
        if len(self._v) == 1:
            return self._v[0]
        return self

    def unstack(self, level=-1, fill_value=None):
        from .reshape import unstack
        return unstack(self, level)

    def memory_usage(self, index=True, deep=False):
        return 8 * len(self._v)

    def view(self):
        return self

    def bool(self):
        return bool(self._v[0])

    def isnumeric(self):
        return self._dt in _NUMDT

    def add_prefix(self, p):
        return self.rename(lambda x: p + str(x))

    def add_suffix(self, p):
        return self.rename(lambda x: str(x) + p)

    def filter(self, items=None, like=None, regex=None, axis=None):
        ix = self._idx._v
        if items is not None:
            pos = [i for i in range(len(ix)) if ix[i] in items]
        elif like is not None:
            pos = [i for i in range(len(ix)) if like in str(ix[i])]
        else:
            import re
            rx = re.compile(regex)
            pos = [i for i in range(len(ix)) if rx.search(str(ix[i]))]
        return self._take(pos)

    def first_valid_index(self):
        for i, x in zip(self._idx._v, self._v):
            if x == x:
                return i
        return None

    def last_valid_index(self):
        for i, x in zip(self._idx._v[::-1], self._v[::-1]):
            if x == x:
                return i
        return None

    def case_when(self, caselist):
        r = self.copy()
        for cond, rep in caselist:
            c = _lst(cond)
            rv = _lst(rep) if _is_listlike(rep) else [rep] * len(c)
            r = r._new([b if k else a for a, k, b in zip(r._v, c, rv)])
        return r

    def interpolate(self, method='linear', **kw):
        v = list(self._v)
        n = len(v)
        i = 0
        while i < n:
            if v[i] != v[i]:
                j = i
                while j < n and v[j] != v[j]:
                    j += 1
                if i > 0 and j < n:
                    a, b = v[i - 1], v[j]
                    for k in range(i, j):
                        v[k] = a + (b - a) * (k - i + 1) / (j - i + 1)
                elif i > 0:
                    for k in range(i, j):
                        v[k] = v[i - 1]
                i = j
            else:
                i += 1
        return self._new(v, 'float64')


def _quant(a, q):
    n = len(a)
    if n == 0:
        return nan
    pos = q * (n - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return float(a[lo])
    f = pos - lo
    return float(a[lo] + (a[hi] - a[lo]) * f)


def _pctlab(p):
    x = p * 100
    if x == int(x):
        return '%d%%' % int(x)
    return ('%g' % x) + '%'


def _pairs(a, b):
    x = []
    y = []
    pb = b._idx._mkpos()
    for k, v in zip(a._idx._v, a._v):
        if k in pb:
            w = b._v[pb[k]]
            if v == v and w == w:
                x.append(v)
                y.append(w)
    return x, y


def _rank_list(a):
    s = Series(a)
    return s.rank()._v


# ================================================================ DataFrame

def _lred(name, v, ddof=1):
    """reduce a python list (skipping missing values); used by groupby / row wise reductions"""
    if name == 'count':
        c = 0
        for x in v:
            if not (x is None or x != x):
                c += 1
        return c
    nn = [x for x in v if not (x is None or x != x)]
    n = len(nn)
    if name == 'size':
        return len(v)
    if name == 'sum':
        if n == 0:
            return 0
        if isinstance(nn[0], str):
            t = nn[0]
            for x in nn[1:]:
                t = t + x
            return t
        return sum(nn)
    if name == 'min':
        return min(nn) if n else nan
    if name == 'max':
        return max(nn) if n else nan
    if name == 'first':
        return nn[0] if n else nan
    if name == 'last':
        return nn[-1] if n else nan
    if name == 'nunique':
        return len(set(nn))
    if name == 'prod':
        p = 1
        for x in nn:
            p = p * x
        return p
    if name == 'any':
        for x in nn:
            if x:
                return True
        return False
    if name == 'all':
        for x in nn:
            if not x:
                return False
        return True
    if n == 0:
        return nan
    if name == 'mean':
        return sum(nn) / n
    if name == 'median':
        s = sorted(nn)
        if n % 2:
            return float(s[n // 2])
        return (s[n // 2 - 1] + s[n // 2]) / 2.0
    if name == 'var' or name == 'std':
        if n - ddof <= 0:
            return nan
        m = sum(nn) / n
        t = 0.0
        for x in nn:
            t += (x - m) * (x - m)
        t = t / (n - ddof)
        return math.sqrt(t) if name == 'std' else t
    if name == 'sem':
        if n - ddof <= 0:
            return nan
        m = sum(nn) / n
        t = 0.0
        for x in nn:
            t += (x - m) * (x - m)
        return math.sqrt(t / (n - ddof)) / math.sqrt(n)
    if name == 'mode':
        cnt = {}
        for x in nn:
            cnt[x] = cnt.get(x, 0) + 1
        mx = max(cnt.values())
        return sorted([k for k, c in cnt.items() if c == mx])[0]
    raise AttributeError(name)


class _Row:
    """namedtuple stand-in for itertuples"""

    def __init__(self, name, fields, values):
        self._name = name
        self._fields = fields
        self._values = values

    def __getattr__(self, n):
        if n.startswith('_'):
            raise AttributeError(n)
        f = self._fields
        if n in f:
            return self._values[f.index(n)]
        raise AttributeError(n)

    def __getitem__(self, i):
        return self._values[i]

    def __len__(self):
        return len(self._values)

    def __iter__(self):
        return iter(self._values)

    def __eq__(self, o):
        return tuple(self._values) == tuple(o)

    def __hash__(self):
        return hash(tuple(self._values))

    def __repr__(self):
        return '%s(%s)' % (self._name, ', '.join(['%s=%r' % (f, v) for f, v in zip(self._fields, self._values)]))

    def _asdict(self):
        return dict(zip(self._fields, self._values))


class DataFrame:
    def __init__(self, data=None, index=None, columns=None, dtype=None, copy=None, _fast=None):
        if _fast is not None:
            self._vals, self._dts, self._cols, self._idx = _fast
            return
        cols = None
        if columns is not None:
            cols = _to_index(columns)
        vals = []
        names = []
        idx = None
        if index is not None:
            idx = _to_index(index)
        if data is None:
            data = {}
        if isinstance(data, DataFrame):
            vals = [list(v) for v in data._vals]
            names = list(data._cols._v)
            if idx is None:
                idx = data._idx
            elif not idx.equals(data._idx):
                r = data.reindex(idx)
                vals = [list(v) for v in r._vals]
            if cols is not None:
                sel = [names.index(c) if c in names else -1 for c in cols._v]
                vals = [vals[k] if k >= 0 else [nan] * len(idx) for k in sel]
                names = list(cols._v)
        elif isinstance(data, dict):
            keys = list(data.keys())
            if cols is not None:
                keys = list(cols._v)
            if len(data) == 0 and index is None:
                idx = RangeIndex(0)
            items = [data[k] if k in data else None for k in keys]
            need_idx = idx is None
            n = None
            if need_idx:
                sidx = None
                for k in keys:
                    x = data.get(k)
                    if isinstance(x, Series):
                        sidx = x._idx if sidx is None else (sidx if sidx._v == x._idx._v else sidx.union(x._idx))
                    elif isinstance(x, dict):
                        xi = Index(list(x.keys()))
                        sidx = xi if sidx is None else (sidx if sidx._v == xi._v else sidx.union(xi))
                    elif _is_listlike(x):
                        L = len(_lst(x))
                        if n is None:
                            n = L
                        elif n != L:
                            raise ValueError('All arrays must be of the same length')
                if sidx is not None:
                    idx = sidx
                    if n is not None and n != len(idx):
                        raise ValueError('array length %d does not match index length %d' % (n, len(idx)))
                elif n is not None:
                    idx = RangeIndex(n)
                else:
                    if len(keys) > 0:
                        raise ValueError('If using all scalar values, you must pass an index')
                    idx = RangeIndex(0)
            for k, x in zip(keys, items):
                if x is None and k not in data:
                    vals.append([nan] * len(idx))
                    names.append(k)
                    continue
                v, dt = _col_from(x, idx)
                vals.append(v)
                names.append(k)
        elif isinstance(data, (list, tuple)) or isinstance(data, np.ndarray) or isinstance(data, (Index, ObjArray)) or _is_iter(data):
            if isinstance(data, np.ndarray):
                data = data.tolist()
            elif isinstance(data, ObjArray):
                data = data._a
            elif not isinstance(data, list):
                data = list(data)
            if len(data) == 0:
                names = []
                vals = []
                if cols is not None:
                    names = list(cols._v)
                    vals = [[] for _ in names]
            else:
                first = data[0]
                if isinstance(first, dict):
                    keys = []
                    seen = {}
                    for r in data:
                        for k in r:
                            if k not in seen:
                                seen[k] = 1
                                keys.append(k)
                    if cols is not None:
                        keys = list(cols._v)
                    for k in keys:
                        vals.append([r.get(k, nan) for r in data])
                    names = keys
                    if idx is None:
                        idx = RangeIndex(len(data))
                elif isinstance(first, Series):
                    keys = []
                    seen = {}
                    for r in data:
                        for k in r._idx._v:
                            if k not in seen:
                                seen[k] = 1
                                keys.append(k)
                    if cols is not None:
                        keys = list(cols._v)
                    for k in keys:
                        col = []
                        for r in data:
                            p = r._idx._mkpos()
                            col.append(r._v[p[k]] if k in p else nan)
                        vals.append(col)
                    names = keys
                    if idx is None:
                        nm = [r.name for r in data]
                        idx = Index(nm) if all([x is not None for x in nm]) else RangeIndex(len(data))
                elif isinstance(first, (list, tuple, np.ndarray)):
                    rows = [list(r) if not isinstance(r, np.ndarray) else r.tolist() for r in data]
                    nc = len(rows[0])
                    for r in rows:
                        if len(r) != nc:
                            raise ValueError('rows have different lengths')
                    vals = [[r[j] for r in rows] for j in range(nc)]
                    names = list(cols._v) if cols is not None else list(range(nc))
                    if cols is not None and len(names) != nc:
                        raise ValueError('%d columns passed, passed data had %d columns' % (len(names), nc))
                else:
                    vals = [list(data)]
                    names = list(cols._v) if cols is not None else [0]
        elif isinstance(data, Series):
            vals = [list(data._v)]
            names = [data.name if data.name is not None else 0]
            if idx is None:
                idx = data._idx
            if cols is not None:
                names = list(cols._v)
        else:
            raise ValueError('DataFrame constructor not properly called!')
        if idx is None:
            idx = RangeIndex(len(vals[0]) if vals else 0)
        dts = []
        for j in range(len(vals)):
            v = vals[j]
            if len(v) != len(idx):
                raise ValueError('Length of values (%d) does not match length of index (%d)' % (len(v), len(idx)))
            v, dt = infer(v)
            vals[j] = v
            dts.append(dt)
        if dtype is not None:
            dn = norm_dtype(dtype)
            for j in range(len(vals)):
                if dts[j] != dn:
                    vals[j] = _astype_vals(vals[j], dts[j], dn)
                    dts[j] = dn
        if cols is not None and not isinstance(data, dict) and not isinstance(data, DataFrame):
            names = list(cols._v) if len(cols._v) == len(vals) else names
            cx = cols
        else:
            cx = Index(names) if not (names and isinstance(names[0], tuple)) else MultiIndex.from_tuples(names)
            if cols is not None and cols.name is not None:
                cx.name = cols.name
        if len(vals) == 0 and cols is not None:
            cx = cols
        self._vals = vals
        self._dts = dts
        self._cols = cx
        self._idx = idx

    @staticmethod
    def _fast(vals, dts, cols, idx):
        return DataFrame(_fast=(vals, dts, cols, idx))

    # ------------------------------------------------------------ properties
    @property
    def index(self):
        return self._idx

    @index.setter
    def index(self, value):
        ix = _to_index(value, len(self._idx))
        if len(ix) != len(self._idx):
            raise ValueError('Length mismatch: Expected axis has %d elements, new values have %d elements' % (len(self._idx), len(ix)))
        self._idx = ix

    @property
    def columns(self):
        return self._cols

    @columns.setter
    def columns(self, value):
        ix = _to_index(value)
        if len(ix) != len(self._cols):
            raise ValueError('Length mismatch: Expected axis has %d elements, new values have %d elements' % (len(self._cols), len(ix)))
        self._cols = ix

    @property
    def shape(self):
        return (len(self._idx), len(self._vals))

    @property
    def size(self):
        return len(self._idx) * len(self._vals)

    @property
    def ndim(self):
        return 2

    @property
    def empty(self):
        return len(self._idx) == 0 or len(self._vals) == 0

    @property
    def axes(self):
        return [self._idx, self._cols]

    @property
    def dtypes(self):
        return Series([dtype_of(d) for d in self._dts], self._cols, _dt='object')

    @property
    def values(self):
        return self.to_numpy()

    @property
    def T(self):
        return self.transpose()

    @property
    def loc(self):
        return _Loc(self, 'loc')

    @property
    def iloc(self):
        return _Loc(self, 'iloc')

    @property
    def at(self):
        return _At(self, 'loc')

    @property
    def iat(self):
        return _At(self, 'iloc')

    def __len__(self):
        return len(self._idx)

    def __iter__(self):
        return iter(self._cols._v)

    def __contains__(self, key):
        return key in self._cols

    def __bool__(self):
        raise ValueError('The truth value of a DataFrame is ambiguous. Use a.empty, a.bool(), a.item(), a.any() or a.all().')

    def __hash__(self):
        raise TypeError("unhashable type: 'DataFrame'")

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        cols = self._cols
        if name in cols:
            return self._col(cols._mkpos()[name])
        raise AttributeError("'DataFrame' object has no attribute '%s'" % name)

    def __dir__(self):
        return list(self._cols._v)

    # ------------------------------------------------------------ helpers
    def _col(self, j):
        return Series(list(self._vals[j]), self._idx, name=self._cols._v[j], _dt=self._dts[j])

    def _colpos(self, key):
        p = self._cols._mkpos()
        if key in p:
            return p[key]
        if isinstance(key, str) and self._cols.nlevels > 1:
            pass
        raise KeyError(key)

    def _like(self, vals, dts, cols=None, idx=None):
        return DataFrame._fast(vals, dts, self._cols if cols is None else cols, self._idx if idx is None else idx)

    def _mapcols(self, f, numeric_only=False):
        """apply f(Series) -> Series to each column"""
        vals = []
        dts = []
        names = []
        for j in range(len(self._vals)):
            if numeric_only and self._dts[j] not in _NUMDT:
                continue
            r = f(self._col(j))
            vals.append(r._v)
            dts.append(r._dt)
            names.append(self._cols._v[j])
        cols = self._cols if len(names) == len(self._vals) else Index(names)
        return DataFrame._fast(vals, dts, cols, self._idx)

    def copy(self, deep=True):
        return DataFrame._fast([list(v) for v in self._vals], list(self._dts), self._cols, self._idx)

    def _take_rows(self, pos):
        iv = self._idx._v
        nix = self._idx._new([iv[i] for i in pos], self._idx.name)
        vals = [[v[i] for i in pos] for v in self._vals]
        return DataFrame._fast(vals, list(self._dts), self._cols, nix)

    def _take_cols(self, pos):
        cv = self._cols._v
        if self._cols.nlevels > 1:
            ncols = MultiIndex(_tuples=[cv[j] for j in pos], names=self._cols.names)
        else:
            ncols = Index([cv[j] for j in pos], name=self._cols.name)
        return DataFrame._fast([list(self._vals[j]) for j in pos], [self._dts[j] for j in pos], ncols, self._idx)

    def _rowseries(self, i):
        vals = [v[i] for v in self._vals]
        vals, dt = infer(vals)
        return Series(vals, self._cols, name=self._idx._v[i], _dt=dt)

    # ------------------------------------------------------------ repr / output
    def __repr__(self):
        return _fmt.frame_repr(self)

    __str__ = __repr__

    def to_string(self, buf=None, index=True, header=True, **kw):
        s = _fmt.frame_repr(self)
        if buf is not None:
            buf.write(s)
            return None
        return s

    def head(self, n=5):
        L = len(self._idx)
        if n >= 0:
            return self._take_rows(list(range(min(n, L))))
        return self._take_rows(list(range(max(L + n, 0))))

    def tail(self, n=5):
        L = len(self._idx)
        if n >= 0:
            return self._take_rows(list(range(max(L - n, 0), L)))
        return self._take_rows(list(range(min(-n, L), L)))

    def to_numpy(self, dtype=None, copy=False, na_value=None):
        rows = [list(r) for r in zip(*self._vals)] if self._vals else [[] for _ in range(len(self._idx))]
        allnum = True
        for d in self._dts:
            if d not in _NUMDT:
                allnum = False
        if allnum and self._vals:
            kinds = set(self._dts)
            if 'bool' in kinds and len(kinds) > 1:
                return ObjArray(rows)
            a = np.array(rows)
        else:
            a = ObjArray(rows)
        if dtype is not None:
            return a.astype(dtype)
        return a

    def to_dict(self, orient='dict', into=dict, index=True):
        cols = self._cols._v
        iv = self._idx._v
        if orient == 'dict':
            return {c: dict(zip(iv, v)) for c, v in zip(cols, self._vals)}
        if orient == 'list':
            return {c: list(v) for c, v in zip(cols, self._vals)}
        if orient == 'records':
            return [dict(zip(cols, r)) for r in zip(*self._vals)] if self._vals else [{} for _ in iv]
        if orient == 'index':
            return {i: dict(zip(cols, r)) for i, r in zip(iv, zip(*self._vals))}
        if orient == 'split':
            return {'index': list(iv), 'columns': list(cols), 'data': [list(r) for r in zip(*self._vals)]}
        if orient == 'series':
            return {c: self._col(j) for j, c in enumerate(cols)}
        if orient == 'tight':
            return {'index': list(iv), 'columns': list(cols), 'data': [list(r) for r in zip(*self._vals)], 'index_names': [self._idx.name], 'column_names': [self._cols.name]}
        raise ValueError("orient '%s' not understood" % orient)

    def to_records(self, index=True):
        return [tuple(r) for r in zip(*self._vals)]

    def to_csv(self, path_or_buf=None, **kw):
        from .pio import to_csv
        return to_csv(self, path_or_buf, **kw)

    def to_json(self, path_or_buf=None, orient='columns', **kw):
        import json
        if orient == 'records':
            d = self.to_dict('records')
        elif orient == 'columns':
            d = self.to_dict('dict')
            d = {str(k): {str(i): x for i, x in v.items()} for k, v in d.items()}
        else:
            d = self.to_dict(orient)
        s = json.dumps(d)
        if path_or_buf is None:
            return s
        f = open(path_or_buf, 'w')
        f.write(s)
        f.close()
        return None

    def items(self):
        return [(c, self._col(j)) for j, c in enumerate(self._cols._v)]

    iteritems = items

    def iterrows(self):
        iv = self._idx._v
        out = []
        for i in range(len(iv)):
            out.append((iv[i], self._rowseries(i)))
        return iter(out)

    def itertuples(self, index=True, name='Pandas'):
        fields = [str(c) for c in self._cols._v]
        if index:
            fields = ['Index'] + fields
        iv = self._idx._v
        i = 0
        out = []
        for r in zip(*self._vals):
            vals = [iv[i]] + list(r) if index else list(r)
            i += 1
            if name is None:
                out.append(tuple(vals))
            else:
                out.append(_Row(name, fields, vals))
        return iter(out)

    def keys(self):
        return self._cols

    # ------------------------------------------------------------ selection
    def __getitem__(self, key):
        if isinstance(key, slice):
            n = len(self._idx)
            st, sp = key.start, key.stop
            if (st is not None and not isinstance(st, int)) or (sp is not None and not isinstance(sp, int)):
                return self._getitem_ix(key, 'loc')
            return self._take_rows(list(range(n))[key])
        if isinstance(key, DataFrame):
            return self.where(key)
        if callable(key):
            return self[key(self)]
        if _is_listlike(key):
            kl = _lst(key)
            if _is_boolvec(kl) and (len(kl) == len(self._idx)):
                return self._take_rows([i for i in range(len(kl)) if kl[i]])
            if isinstance(key, np.ndarray) and key.ndim == 2:
                raise ValueError('bad key')
            p = self._cols._mkpos()
            pos = []
            miss = []
            for k in kl:
                if k in p:
                    pos.append(p[k])
                else:
                    miss.append(k)
            if miss:
                raise KeyError('%s not in index' % (miss,))
            r = self._take_cols(pos)
            return r
        p = self._cols._mkpos()
        if key in p:
            j = p[key]
            if len(p) != len(self._vals):
                js = [k for k in range(len(self._vals)) if self._cols._v[k] == key]
                if len(js) > 1:
                    return self._take_cols(js)
            return self._col(j)
        if self._cols.nlevels > 1:
            js = [k for k in range(len(self._vals)) if self._cols._v[k][0] == key]
            if js:
                r = self._take_cols(js)
                r._cols = r._cols.droplevel(0)
                if len(js) == 1 and r._cols._v[0] == '':
                    return self._col(js[0])
                return r
        raise KeyError(key)

    def _getitem_ix(self, key, kind):
        if callable(key) and not isinstance(key, (str, tuple)):
            key = key(self)
        n = len(self._idx)
        m = len(self._vals)
        if isinstance(key, tuple) and (self._idx.nlevels == 1 or kind == 'iloc') and len(key) in (1, 2):
            rk = key[0]
            ck = key[1] if len(key) == 2 else slice(None)
        else:
            rk = key
            ck = slice(None)
        if callable(rk) and not isinstance(rk, (str, tuple)):
            rk = rk(self)
        if callable(ck) and not isinstance(ck, (str, tuple)):
            ck = ck(self)
        rp, rs = _positions(self._idx, rk, kind, n)
        if isinstance(ck, slice) and ck.start is None and ck.stop is None and ck.step is None:
            cp, cs = list(range(m)), False
        else:
            cp, cs = _positions(self._cols, ck, kind, m)
        if rs and cs:
            return self._vals[cp[0]][rp[0]]
        if rs:
            vals = [self._vals[j][rp[0]] for j in cp]
            vals, dt = infer(vals)
            return Series(vals, self._cols._new([self._cols._v[j] for j in cp], self._cols.name), name=self._idx._v[rp[0]], _dt=dt)
        if cs:
            j = cp[0]
            v = self._vals[j]
            iv = self._idx._v
            return Series([v[i] for i in rp], self._idx._new([iv[i] for i in rp], self._idx.name), name=self._cols._v[j], _dt=self._dts[j])
        r = self._take_rows(rp) if not (len(rp) == n and rp == list(range(n))) else self.copy()
        if len(cp) != m or cp != list(range(m)):
            r = r._take_cols(cp)
        if kind == 'loc' and self._idx.nlevels > 1 and (not _is_listlike(rk) or isinstance(rk, tuple)) and not isinstance(rk, slice):
            k = len(rk) if isinstance(rk, tuple) else 1
            if k < self._idx.nlevels:
                r._idx = r._idx.droplevel(list(range(k))) if k > 1 else r._idx.droplevel(0)
        return r

    def _setitem_ix(self, key, value, kind):
        n = len(self._idx)
        m = len(self._vals)
        if isinstance(key, tuple) and (self._idx.nlevels == 1 or kind == 'iloc') and len(key) in (1, 2):
            rk = key[0]
            ck = key[1] if len(key) == 2 else slice(None)
        else:
            rk = key
            ck = slice(None)
        if callable(rk) and not isinstance(rk, (str, tuple)):
            rk = rk(self)
        # enlargement by row label
        if kind == 'loc' and not _is_listlike(rk) and not isinstance(rk, slice) and rk not in self._idx:
            self._append_row(rk, ck, value)
            return
        rp, rs = _positions(self._idx, rk, kind, n)
        newcol = None
        if kind == 'loc' and not isinstance(ck, slice) and not _is_listlike(ck) and ck not in self._cols:
            newcol = ck
        elif kind == 'loc' and _is_listlike(ck):
            for c in _lst(ck):
                if c not in self._cols:
                    self._insert_col(c, [nan] * n, 'float64')
            m = len(self._vals)
        if newcol is not None:
            self._insert_col(newcol, [nan] * n, 'float64')
            m = len(self._vals)
        if isinstance(ck, slice) and ck.start is None and ck.stop is None:
            cp = list(range(m))
        else:
            cp, cs = _positions(self._cols, ck, kind, m)
        nr = len(rp)
        nc = len(cp)
        if isinstance(value, DataFrame):
            srcs = []
            for jj, j in enumerate(cp):
                c = self._cols._v[j]
                if c in value._cols:
                    sv = value._col(value._cols._mkpos()[c])
                    p = sv._idx._mkpos()
                    srcs.append([sv._v[p[self._idx._v[i]]] if self._idx._v[i] in p else nan for i in rp])
                else:
                    srcs.append([nan] * nr)
        elif _is_listlike(value):
            vl = _lst(value)
            if isinstance(value, Series) and nc >= 1:
                p = value._idx._mkpos()
                if nc == 1 or nr != 1:
                    one = [vl[p[self._idx._v[i]]] if self._idx._v[i] in p else nan for i in rp] if nc == 1 else None
                    if nc == 1:
                        srcs = [one]
                    else:
                        srcs = [[vl[p[self._cols._v[j]]] if self._cols._v[j] in p else nan for _ in rp] for j in cp]
                else:
                    srcs = [[vl[p[self._cols._v[j]]]] for j in cp]
            elif nc == 1 and (len(vl) == nr and not (len(vl) > 0 and isinstance(vl[0], (list, tuple)))):
                srcs = [vl]
            elif nc == 1 and len(vl) > 0 and isinstance(vl[0], (list, tuple)):
                srcs = [[r[0] for r in vl]]
            elif nr == 1 and len(vl) == nc and not (len(vl) > 0 and isinstance(vl[0], (list, tuple))):
                srcs = [[x] for x in vl]
            elif len(vl) == nr and len(vl) > 0 and isinstance(vl[0], (list, tuple)):
                srcs = [[r[k] for r in vl] for k in range(nc)]
            else:
                raise ValueError('Must have equal len keys and value when setting with an iterable')
        else:
            srcs = [[value] * nr for _ in cp]
        for jj in range(nc):
            j = cp[jj]
            col = list(self._vals[j])
            src = srcs[jj]
            for k in range(nr):
                col[rp[k]] = src[k]
            col, dt = infer(col)
            self._vals[j] = col
            self._dts[j] = dt

    def _append_row(self, label, ck, value):
        m = len(self._vals)
        if isinstance(ck, slice):
            if isinstance(value, dict):
                row = [value.get(c, nan) for c in self._cols._v]
            elif isinstance(value, Series):
                p = value._idx._mkpos()
                row = [value._v[p[c]] if c in p else nan for c in self._cols._v]
            elif _is_listlike(value):
                row = list(_lst(value))
            else:
                row = [value] * m
            if len(row) != m:
                raise ValueError('cannot set a row with mismatched columns')
        else:
            if ck not in self._cols:
                self._insert_col(ck, [nan] * len(self._idx), 'float64')
                m = len(self._vals)
            j = self._cols._mkpos()[ck]
            row = [nan] * m
            for q in range(m):
                col, dt = infer(self._vals[q] + [nan])
                self._vals[q] = col
                self._dts[q] = dt
            col = list(self._vals[j])
            col[-1] = value
            col, dt = infer(col)
            self._vals[j] = col
            self._dts[j] = dt
            self._idx = Index(self._idx._v + [label], name=self._idx.name) if not isinstance(self._idx, MultiIndex) else MultiIndex(_tuples=self._idx._v + [label], names=self._idx.names)
            return
        for j in range(m):
            col = self._vals[j] + [row[j]]
            col, dt = infer(col)
            self._vals[j] = col
            self._dts[j] = dt
        self._idx = Index(self._idx._v + [label], name=self._idx.name) if not isinstance(self._idx, MultiIndex) else MultiIndex(_tuples=self._idx._v + [label], names=self._idx.names)

    def _insert_col(self, name, vals, dt, loc=None):
        if loc is None or loc >= len(self._vals):
            self._vals = self._vals + [vals]
            self._dts = self._dts + [dt]
            cv = self._cols._v + [name]
        else:
            self._vals = self._vals[:loc] + [vals] + self._vals[loc:]
            self._dts = self._dts[:loc] + [dt] + self._dts[loc:]
            cv = self._cols._v[:loc] + [name] + self._cols._v[loc:]
        if self._cols.nlevels > 1:
            self._cols = MultiIndex(_tuples=cv, names=self._cols.names)
        else:
            self._cols = Index(cv, name=self._cols.name)

    def __setitem__(self, key, value):
        if isinstance(key, DataFrame):
            return
        if _is_listlike(key) and not isinstance(key, tuple):
            kl = _lst(key)
            if _is_boolvec(kl) and len(kl) == len(self._idx):
                self._setitem_ix((kl, slice(None)), value, 'loc')
                return
            if isinstance(value, DataFrame):
                for k, c in enumerate(kl):
                    self[c] = value._col(k)
            elif isinstance(value, np.ndarray) and value.ndim == 2:
                for k, c in enumerate(kl):
                    self[c] = value[:, k]
            elif _is_listlike(value) and len(_lst(value)) > 0 and isinstance(_lst(value)[0], (list, tuple)) and len(_lst(value)[0]) == len(kl):
                rows = _lst(value)
                for k, c in enumerate(kl):
                    self[c] = [r[k] for r in rows]
            else:
                for c in kl:
                    self[c] = value
            return
        v, dt = _col_from(value, self._idx) if (len(self._vals) > 0 or len(self._idx) > 0 or isinstance(value, Series)) else _col_from_empty(value, self)
        p = self._cols._mkpos()
        if key in p:
            j = p[key]
            self._vals[j] = v
            self._dts[j] = dt
        else:
            self._insert_col(key, v, dt)

    def __delitem__(self, key):
        r = self.drop(columns=[key])
        self._vals, self._dts, self._cols = r._vals, r._dts, r._cols

    def insert(self, loc, column, value, allow_duplicates=False):
        v, dt = _col_from(value, self._idx)
        self._insert_col(column, v, dt, loc)

    def pop(self, item):
        r = self[item]
        del self[item]
        return r

    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    def assign(self, **kwargs):
        r = self.copy()
        for k, v in kwargs.items():
            if callable(v):
                v = v(r)
            r[k] = v
        return r

    def drop(self, labels=None, axis=0, index=None, columns=None, level=None, inplace=False, errors='raise'):
        if labels is not None:
            if axis == 1 or axis == 'columns':
                columns = labels
            else:
                index = labels
        r = self
        if columns is not None:
            cl = _lst(columns) if _is_listlike(columns) else [columns]
            if errors == 'raise':
                for c in cl:
                    if c not in self._cols:
                        raise KeyError('%s not found in axis' % ([c],))
            s = set(cl)
            keep = [j for j in range(len(self._vals)) if self._cols._v[j] not in s]
            r = r._take_cols(keep)
        if index is not None:
            il = _lst(index) if _is_listlike(index) else [index]
            if errors == 'raise':
                for c in il:
                    if c not in r._idx:
                        raise KeyError('%s not found in axis' % ([c],))
            s = set(il)
            keep = [i for i in range(len(r._idx)) if r._idx._v[i] not in s]
            r = r._take_rows(keep)
        if r is self:
            r = self.copy()
        if inplace:
            self._vals, self._dts, self._cols, self._idx = r._vals, r._dts, r._cols, r._idx
            return None
        return r

    def rename(self, mapper=None, index=None, columns=None, axis=None, inplace=False, errors='ignore', copy=None, level=None):
        if mapper is not None:
            if axis == 1 or axis == 'columns':
                columns = mapper
            else:
                index = mapper
        r = self.copy()
        if columns is not None:
            if isinstance(columns, dict):
                r._cols = Index([columns.get(x, x) for x in self._cols._v], name=self._cols.name) if self._cols.nlevels == 1 else MultiIndex(_tuples=[tuple([columns.get(y, y) for y in x]) for x in self._cols._v], names=self._cols.names)
            else:
                r._cols = Index([columns(x) for x in self._cols._v], name=self._cols.name)
        if index is not None:
            if isinstance(index, dict):
                r._idx = Index([index.get(x, x) for x in self._idx._v], name=self._idx.name)
            else:
                r._idx = Index([index(x) for x in self._idx._v], name=self._idx.name)
        if inplace:
            self._cols, self._idx = r._cols, r._idx
            return None
        return r

    def rename_axis(self, mapper=None, index=None, columns=None, axis=0, **kw):
        r = self.copy()
        if mapper is not None and index is None and columns is None:
            if axis in (1, 'columns'):
                columns = mapper
            else:
                index = mapper
        if index is not None:
            r._idx = self._idx.copy()
            r._idx.rename(index, inplace=True)
        if columns is not None:
            r._cols = self._cols.copy()
            r._cols.rename(columns, inplace=True)
        return r

    def set_axis(self, labels, axis=0, copy=True):
        r = self.copy()
        if axis in (1, 'columns'):
            r.columns = labels
        else:
            r.index = labels
        return r

    def add_prefix(self, p, axis=None):
        return self.rename(columns=lambda x: p + str(x))

    def add_suffix(self, p, axis=None):
        return self.rename(columns=lambda x: str(x) + p)

    def set_index(self, keys, drop=True, append=False, inplace=False, verify_integrity=False):
        kl = list(keys) if isinstance(keys, (list, tuple)) and not (isinstance(keys, tuple) and keys in self._cols) else [keys]
        arrays = []
        names = []
        dropc = []
        for k in kl:
            if isinstance(k, (Series, Index, np.ndarray)) or (isinstance(k, list) and k not in self._cols._v):
                arrays.append(list(_lst(k)))
                names.append(getattr(k, 'name', None))
            else:
                j = self._colpos(k)
                arrays.append(self._vals[j])
                names.append(k)
                dropc.append(k)
        if append:
            if self._idx.nlevels == 1:
                arrays = [self._idx._v] + arrays
                names = [self._idx.name] + names
            else:
                arrays = [[t[q] for t in self._idx._v] for q in range(self._idx.nlevels)] + arrays
                names = self._idx.names + names
        if len(arrays) == 1:
            ix = Index(list(arrays[0]), name=names[0])
        else:
            ix = MultiIndex(_tuples=list(zip(*arrays)), names=names)
        r = self
        if drop and dropc:
            r = self.drop(columns=dropc)
        else:
            r = self.copy()
        r._idx = ix
        if inplace:
            self._vals, self._dts, self._cols, self._idx = r._vals, r._dts, r._cols, r._idx
            return None
        return r

    def reset_index(self, level=None, drop=False, inplace=False, col_level=0, col_fill='', names=None, allow_duplicates=False):
        r = self.copy()
        idx = self._idx
        if not drop:
            ins = []
            if idx.nlevels == 1:
                nm = idx.name
                if nm is None:
                    nm = 'index' if 'index' not in self._cols else 'level_0'
                ins.append((nm, list(idx._v)))
            else:
                for k in range(idx.nlevels):
                    nm = idx.names[k] if idx.names[k] is not None else 'level_%d' % k
                    ins.append((nm, [t[k] for t in idx._v]))
            if level is not None:
                lv = level if isinstance(level, list) else [level]
                ins = [t for k, t in enumerate(ins) if t[0] in lv or k in lv]
            pos = 0
            for nm, v in ins:
                v2, dt = infer(list(v))
                r._insert_col(nm, v2, dt, pos)
                pos += 1
        if level is not None and idx.nlevels > 1:
            lv = level if isinstance(level, list) else [level]
            keep = [k for k in range(idx.nlevels) if not (idx.names[k] in lv or k in lv)]
            if len(keep) == 1:
                r._idx = Index([t[keep[0]] for t in idx._v], name=idx.names[keep[0]])
            else:
                r._idx = MultiIndex(_tuples=[tuple([t[k] for k in keep]) for t in idx._v], names=[idx.names[k] for k in keep])
        else:
            r._idx = RangeIndex(len(idx))
        if inplace:
            self._vals, self._dts, self._cols, self._idx = r._vals, r._dts, r._cols, r._idx
            return None
        return r

    def reindex(self, index=None, columns=None, labels=None, axis=None, fill_value=nan, method=None, copy=None, **kw):
        if labels is not None:
            if axis in (1, 'columns'):
                columns = labels
            else:
                index = labels
        r = self
        if index is not None:
            ix = _to_index(index)
            p = self._idx._mkpos()
            if method in ('ffill', 'pad'):
                allix = Index(sortkey_list(list(set(ix._v) | set(self._idx._v))))
                r = self.reindex(index=allix).ffill().reindex(index=ix)
            else:
                pos = [p.get(k, -1) for k in ix._v]
                vals = []
                for v in r._vals:
                    vals.append([v[i] if i >= 0 else fill_value for i in pos])
                nd = []
                nv = []
                for v in vals:
                    v2, dt = infer(v)
                    nv.append(v2)
                    nd.append(dt)
                r = DataFrame._fast(nv, nd, r._cols, ix)
        if columns is not None:
            cx = _to_index(columns)
            p = r._cols._mkpos()
            vals = []
            dts = []
            for c in cx._v:
                if c in p:
                    vals.append(list(r._vals[p[c]]))
                    dts.append(r._dts[p[c]])
                else:
                    v, dt = infer([fill_value] * len(r._idx))
                    vals.append(v)
                    dts.append(dt)
            r = DataFrame._fast(vals, dts, cx, r._idx)
        if r is self:
            r = self.copy()
        return r

    def align(self, other, join='outer', axis=None):
        return self, other

    def take(self, indices, axis=0):
        if axis == 1:
            return self._take_cols(list(indices))
        return self._take_rows([i if i >= 0 else i + len(self._idx) for i in indices])

    def filter(self, items=None, like=None, regex=None, axis=None):
        cv = self._cols._v
        if items is not None:
            pos = [j for j in range(len(cv)) if cv[j] in items]
        elif like is not None:
            pos = [j for j in range(len(cv)) if like in str(cv[j])]
        else:
            import re
            rx = re.compile(regex)
            pos = [j for j in range(len(cv)) if rx.search(str(cv[j]))]
        return self._take_cols(pos)

    def select_dtypes(self, include=None, exclude=None):
        def match(dt, spec):
            if spec is None:
                return False
            specs = spec if isinstance(spec, (list, tuple)) else [spec]
            for s in specs:
                if s == 'number':
                    if dt in ('int64', 'float64'):
                        return True
                elif s in ('object', object):
                    if dt in ('object',):
                        return True
                elif s in ('str', 'string', str):
                    if dt == 'str':
                        return True
                elif norm_dtype(s) == dt:
                    return True
                elif s == 'category':
                    pass
            return False
        pos = []
        for j, dt in enumerate(self._dts):
            ok = True
            if include is not None:
                ok = match(dt, include)
            if exclude is not None and match(dt, exclude):
                ok = False
            if ok:
                pos.append(j)
        return self._take_cols(pos)

    # ------------------------------------------------------------ arithmetic
    def _binop(self, other, op, rev=False):
        n = len(self._idx)
        if isinstance(other, DataFrame):
            if self._idx._v == other._idx._v and self._cols._v == other._cols._v:
                vals = []
                dts = []
                for j in range(len(self._vals)):
                    if rev:
                        v, dt = _calc(op, other._vals[j], other._dts[j], self._vals[j], self._dts[j])
                    else:
                        v, dt = _calc(op, self._vals[j], self._dts[j], other._vals[j], other._dts[j])
                    vals.append(v)
                    dts.append(dt)
                return DataFrame._fast(vals, dts, self._cols, self._idx)
            idx = self._idx.union(other._idx) if self._idx._v != other._idx._v else self._idx
            cols = self._cols.union(other._cols) if self._cols._v != other._cols._v else self._cols
            a = self.reindex(index=idx, columns=cols)
            b = other.reindex(index=idx, columns=cols)
            return a._binop(b, op, rev)
        if isinstance(other, Series):
            # align with columns
            p = other._idx._mkpos()
            if self._cols._v == other._idx._v:
                cols = self._cols
            else:
                cols = self._cols.union(other._idx)
            vals = []
            dts = []
            for c in cols._v:
                j = self._cols._mkpos().get(c)
                if j is None or c not in p:
                    vals.append([nan] * n)
                    dts.append('float64')
                    continue
                s = other._v[p[c]]
                if rev:
                    sv = Series(list(self._vals[j]), self._idx, _dt=self._dts[j])
                    r = sv._rscalar(op, s)
                    vals.append(r[0])
                    dts.append(r[1])
                else:
                    v, dt = _calc(op, self._vals[j], self._dts[j], s, None)
                    vals.append(v)
                    dts.append(dt)
            return DataFrame._fast(vals, dts, cols, self._idx)
        if isinstance(other, np.ndarray) and other.ndim == 2:
            return self._binop(DataFrame(other, index=self._idx, columns=self._cols), op, rev)
        if _is_listlike(other):
            ol = _lst(other)
            if len(ol) == len(self._vals):
                return self._binop(Series(ol, self._cols), op, rev)
            raise ValueError('Unable to coerce to Series, length must be %d: given %d' % (len(self._vals), len(ol)))
        vals = []
        dts = []
        for j in range(len(self._vals)):
            if rev:
                sv = Series(self._vals[j], self._idx, _dt=self._dts[j])
                v, dt = sv._rscalar(op, other)
            else:
                v, dt = _calc(op, self._vals[j], self._dts[j], other, None)
            vals.append(v)
            dts.append(dt)
        return DataFrame._fast(vals, dts, self._cols, self._idx)

    def _binop_axis(self, other, op, axis, rev=False):
        if isinstance(other, Series) and axis in (0, 'index'):
            vals = []
            dts = []
            for j in range(len(self._vals)):
                s = Series(self._vals[j], self._idx, _dt=self._dts[j])
                r = s._binop(other, op, rev)
                vals.append(r._v)
                dts.append(r._dt)
            idx = self._idx if self._idx._v == other._idx._v else self._idx.union(other._idx)
            return DataFrame._fast(vals, dts, self._cols, idx)
        return self._binop(other, op, rev)

    def __add__(self, o):
        return self._binop(o, '+')

    def __radd__(self, o):
        return self._binop(o, '+', True)

    def __sub__(self, o):
        return self._binop(o, '-')

    def __rsub__(self, o):
        return self._binop(o, '-', True)

    def __mul__(self, o):
        return self._binop(o, '*')

    def __rmul__(self, o):
        return self._binop(o, '*', True)

    def __truediv__(self, o):
        return self._binop(o, '/')

    def __rtruediv__(self, o):
        return self._binop(o, '/', True)

    def __floordiv__(self, o):
        return self._binop(o, '//')

    def __mod__(self, o):
        return self._binop(o, '%')

    def __pow__(self, o):
        return self._binop(o, '**')

    def __eq__(self, o):
        return self._binop(o, '==')

    def __ne__(self, o):
        return self._binop(o, '!=')

    def __lt__(self, o):
        return self._binop(o, '<')

    def __le__(self, o):
        return self._binop(o, '<=')

    def __gt__(self, o):
        return self._binop(o, '>')

    def __ge__(self, o):
        return self._binop(o, '>=')

    def __and__(self, o):
        return self._binop(o, '&')

    def __or__(self, o):
        return self._binop(o, '|')

    def __invert__(self):
        return self._mapcols(lambda s: ~s)

    def __neg__(self):
        return self._mapcols(lambda s: -s)

    def __abs__(self):
        return self.abs()

    def add(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '+', axis)

    def sub(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '-', axis)

    def mul(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '*', axis)

    def div(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '/', axis)

    truediv = div

    def floordiv(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '//', axis)

    def mod(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '%', axis)

    def pow(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '**', axis)

    def radd(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '+', axis, True)

    def rsub(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '-', axis, True)

    def rmul(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '*', axis, True)

    def rdiv(self, o, axis='columns', fill_value=None):
        return self._binop_axis(o, '/', axis, True)

    def eq(self, o, axis='columns'):
        return self._binop_axis(o, '==', axis)

    def ne(self, o, axis='columns'):
        return self._binop_axis(o, '!=', axis)

    def lt(self, o, axis='columns'):
        return self._binop_axis(o, '<', axis)

    def le(self, o, axis='columns'):
        return self._binop_axis(o, '<=', axis)

    def gt(self, o, axis='columns'):
        return self._binop_axis(o, '>', axis)

    def ge(self, o, axis='columns'):
        return self._binop_axis(o, '>=', axis)

    def dot(self, other):
        a = self.to_numpy()
        if isinstance(other, DataFrame):
            b = other.to_numpy()
            r = np.dot(a, b)
            return DataFrame(r, index=self._idx, columns=other._cols)
        r = np.dot(a, np.array(_lst(other)))
        return Series(r.tolist(), self._idx)

    def __matmul__(self, other):
        return self.dot(other)

    # ------------------------------------------------------------ elementwise
    def abs(self):
        return self._mapcols(lambda s: s.abs())

    def round(self, decimals=0):
        if isinstance(decimals, dict):
            out = self.copy()
            for k, d in decimals.items():
                out[k] = self[k].round(d)
            return out
        return self._mapcols(lambda s: s.round(decimals) if s._dt == 'float64' else s.copy())

    def clip(self, lower=None, upper=None, axis=None):
        return self._mapcols(lambda s: s.clip(lower, upper) if s._dt in _NUMDT else s.copy())

    def astype(self, dtype, copy=True, errors='raise'):
        if isinstance(dtype, dict):
            r = self.copy()
            for k, d in dtype.items():
                r[k] = r[k].astype(d)
            return r
        return self._mapcols(lambda s: s.astype(dtype))

    def infer_objects(self):
        return self._mapcols(lambda s: s.infer_objects())

    def convert_dtypes(self, *a, **k):
        return self.copy()

    def isna(self):
        return self._mapcols(lambda s: s.isna())

    isnull = isna

    def notna(self):
        return self._mapcols(lambda s: s.notna())

    notnull = notna

    def isin(self, values):
        if isinstance(values, dict):
            def f(s):
                if s.name in values:
                    return s.isin(values[s.name])
                return s._new([False] * len(s._v), 'bool')
            return self._mapcols(f)
        if isinstance(values, DataFrame):
            return self._mapcols(lambda s: s.isin(values[s.name]._v) if s.name in values._cols else s.isin([]))
        return self._mapcols(lambda s: s.isin(values))

    def fillna(self, value=None, method=None, axis=None, inplace=False, limit=None, downcast=None):
        if isinstance(value, dict):
            def f(s):
                if s.name in value:
                    return s.fillna(value[s.name])
                return s.copy()
            r = self._mapcols(f)
        elif isinstance(value, Series):
            r = self._mapcols(lambda s: s.fillna(value[s.name]) if s.name in value._idx else s.copy())
        elif method is not None:
            r = self._mapcols(lambda s: s.fillna(method=method))
        else:
            r = self._mapcols(lambda s: s.fillna(value))
        if inplace:
            self._vals, self._dts = r._vals, r._dts
            return None
        return r

    def ffill(self, limit=None):
        return self._mapcols(lambda s: s.ffill(limit))

    def bfill(self, limit=None):
        return self._mapcols(lambda s: s.bfill(limit))

    pad = ffill
    backfill = bfill

    def interpolate(self, **kw):
        return self._mapcols(lambda s: s.interpolate(**kw) if s._dt in _NUMDT else s.copy())

    def dropna(self, axis=0, how='any', thresh=None, subset=None, inplace=False, ignore_index=False):
        if axis in (1, 'columns'):
            keep = []
            n = len(self._idx)
            for j in range(len(self._vals)):
                c = 0
                for x in self._vals[j]:
                    if not (x is None or x != x):
                        c += 1
                if thresh is not None:
                    ok = c >= thresh
                elif how == 'any':
                    ok = c == n
                else:
                    ok = c > 0
                if ok:
                    keep.append(j)
            r = self._take_cols(keep)
        else:
            cols = range(len(self._vals))
            if subset is not None:
                sl = subset if isinstance(subset, list) else [subset]
                cols = [self._colpos(c) for c in sl]
            n = len(self._idx)
            keep = []
            for i in range(n):
                c = 0
                tot = 0
                for j in cols:
                    x = self._vals[j][i]
                    tot += 1
                    if not (x is None or x != x):
                        c += 1
                if thresh is not None:
                    ok = c >= thresh
                elif how == 'any':
                    ok = c == tot
                else:
                    ok = c > 0
                if ok:
                    keep.append(i)
            r = self._take_rows(keep)
        if ignore_index:
            r._idx = RangeIndex(len(r._idx))
        if inplace:
            self._vals, self._dts, self._cols, self._idx = r._vals, r._dts, r._cols, r._idx
            return None
        return r

    def replace(self, to_replace=None, value=None, inplace=False, regex=False):
        if isinstance(to_replace, dict) and value is None:
            # {col: {old: new}} or {old: new}
            first = list(to_replace.values())[0] if to_replace else None
            if isinstance(first, dict):
                r = self.copy()
                for c, m in to_replace.items():
                    r[c] = r[c].replace(m)
                return r
        r = self._mapcols(lambda s: s.replace(to_replace, value, regex=regex))
        if inplace:
            self._vals, self._dts = r._vals, r._dts
            return None
        return r

    def where(self, cond, other=nan):
        if isinstance(cond, DataFrame):
            vals = []
            dts = []
            for j in range(len(self._vals)):
                c = cond._vals[j]
                o = other._vals[j] if isinstance(other, DataFrame) else [other] * len(c)
                v, dt = infer([x if k else y for x, k, y in zip(self._vals[j], c, o)])
                vals.append(v)
                dts.append(dt)
            return DataFrame._fast(vals, dts, self._cols, self._idx)
        return self._mapcols(lambda s: s.where(cond, other))

    def mask(self, cond, other=nan):
        if isinstance(cond, DataFrame):
            return self.where(~cond, other)
        return self._mapcols(lambda s: s.mask(cond, other))

    def applymap(self, func, na_action=None):
        return self._mapcols(lambda s: s.map(func, na_action))

    def map(self, func, na_action=None):
        return self._mapcols(lambda s: s.map(func, na_action))

    def transform(self, func, axis=0, *a, **k):
        if isinstance(func, str):
            return getattr(self, func)(*a, **k)
        return self._mapcols(lambda s: func(s))

    def pipe(self, func, *args, **kwargs):
        return func(self, *args, **kwargs)

    def apply(self, func, axis=0, raw=False, result_type=None, args=(), **kwargs):
        if isinstance(func, str):
            return getattr(self, func)(*args, **kwargs)
        if axis in (0, 'index'):
            res = []
            for j in range(len(self._vals)):
                res.append(func(self._col(j), *args, **kwargs))
            names = self._cols
            if len(res) > 0 and isinstance(res[0], Series):
                return DataFrame({names._v[j]: res[j] for j in range(len(res))}) if True else None
            v, dt = infer(res)
            return Series(v, names, _dt=dt)
        res = []
        for i in range(len(self._idx)):
            res.append(func(self._rowseries(i), *args, **kwargs))
        if len(res) > 0 and isinstance(res[0], Series):
            r = DataFrame(res)
            r._idx = self._idx
            return r
        if len(res) > 0 and isinstance(res[0], (list, tuple)) and result_type == 'expand':
            return DataFrame(res, index=self._idx)
        v, dt = infer(res)
        return Series(v, self._idx, _dt=dt)

    def agg(self, func=None, axis=0, *args, **kwargs):
        if isinstance(func, str):
            return getattr(self, func)(*args, **kwargs)
        if callable(func):
            return self.apply(func, axis=axis)
        if isinstance(func, (list, tuple)):
            cols = {}
            names = [f if isinstance(f, str) else f.__name__ for f in func]
            for j, c in enumerate(self._cols._v):
                if self._dts[j] not in _NUMDT and all([nm in ('mean', 'std', 'var', 'median') for nm in names]):
                    continue
                s = self._col(j)
                cols[c] = [s.agg(f) for f in func]
            return DataFrame(cols, index=Index(names))
        if isinstance(func, dict):
            out = {}
            allf = all([isinstance(v, str) or callable(v) for v in func.values()])
            if allf:
                return Series([self[k].agg(f) for k, f in func.items()], Index(list(func.keys())))
            names = []
            for k, f in func.items():
                fl = f if isinstance(f, list) else [f]
                for x in fl:
                    nm = x if isinstance(x, str) else x.__name__
                    if nm not in names:
                        names.append(nm)
            for k, f in func.items():
                fl = f if isinstance(f, list) else [f]
                d = {}
                for x in fl:
                    nm = x if isinstance(x, str) else x.__name__
                    d[nm] = self[k].agg(x)
                out[k] = [d.get(nm, nan) for nm in names]
            return DataFrame(out, index=Index(names))
        raise ValueError('bad agg')

    aggregate = agg

    # ------------------------------------------------------------ reductions
    def _reduce(self, name, axis=0, numeric_only=False, skipna=True, ddof=1, only_num=False, **kw):
        if axis in (1, 'columns'):
            cols = [j for j in range(len(self._vals)) if (self._dts[j] in _NUMDT) or not (only_num or numeric_only)]
            n = len(self._idx)
            out = []
            anyf = False
            for j in cols:
                if self._dts[j] == 'float64':
                    anyf = True
            srcs = []
            for j in cols:
                if anyf and self._dts[j] != 'float64' and self._dts[j] in _NUMDT:
                    srcs.append([float(x) for x in self._vals[j]])
                else:
                    srcs.append(self._vals[j])
            for i in range(n):
                row = [c[i] for c in srcs]
                out.append(_lred(name, row, ddof))
            out, dt = infer(out)
            return Series(out, self._idx, _dt=dt)
        vals = []
        names = []
        for j in range(len(self._vals)):
            if self._dts[j] not in _NUMDT and (only_num or numeric_only):
                continue
            s = self._col(j)
            if name == 'sum':
                r = s.sum(skipna=skipna, **kw)
            elif name == 'mean':
                r = s.mean(skipna=skipna)
            elif name == 'median':
                r = s.median()
            elif name == 'std':
                r = s.std(ddof=ddof)
            elif name == 'var':
                r = s.var(ddof=ddof)
            elif name == 'min':
                r = s.min(skipna=skipna)
            elif name == 'max':
                r = s.max(skipna=skipna)
            elif name == 'count':
                r = s.count()
            elif name == 'prod':
                r = s.prod()
            elif name == 'sem':
                r = s.sem(ddof=ddof)
            elif name == 'nunique':
                r = s.nunique()
            elif name == 'skew':
                r = s.skew()
            elif name == 'any':
                r = s.any()
            elif name == 'all':
                r = s.all()
            else:
                r = getattr(s, name)()
            vals.append(r)
            names.append(self._cols._v[j])
        vals, dt = infer(vals)
        return Series(vals, Index(names) if len(names) != len(self._vals) else self._cols, _dt=dt)

    def sum(self, axis=0, skipna=True, numeric_only=False, min_count=0, **kw):
        return self._reduce('sum', axis, numeric_only, skipna)

    def mean(self, axis=0, skipna=True, numeric_only=False, **kw):
        return self._reduce('mean', axis, numeric_only, skipna, only_num=True)

    def median(self, axis=0, skipna=True, numeric_only=False, **kw):
        return self._reduce('median', axis, numeric_only, skipna, only_num=True)

    def std(self, axis=0, skipna=True, ddof=1, numeric_only=False, **kw):
        return self._reduce('std', axis, numeric_only, skipna, ddof, only_num=True)

    def var(self, axis=0, skipna=True, ddof=1, numeric_only=False, **kw):
        return self._reduce('var', axis, numeric_only, skipna, ddof, only_num=True)

    def sem(self, axis=0, skipna=True, ddof=1, numeric_only=False, **kw):
        return self._reduce('sem', axis, numeric_only, skipna, ddof, only_num=True)

    def skew(self, axis=0, skipna=True, numeric_only=False, **kw):
        return self._reduce('skew', axis, numeric_only, skipna, only_num=True)

    def min(self, axis=0, skipna=True, numeric_only=False, **kw):
        return self._reduce('min', axis, numeric_only, skipna)

    def max(self, axis=0, skipna=True, numeric_only=False, **kw):
        return self._reduce('max', axis, numeric_only, skipna)

    def prod(self, axis=0, skipna=True, numeric_only=False, **kw):
        return self._reduce('prod', axis, numeric_only, skipna, only_num=True)

    product = prod

    def count(self, axis=0, numeric_only=False):
        return self._reduce('count', axis, numeric_only)

    def nunique(self, axis=0, dropna=True):
        return self._reduce('nunique', axis)

    def any(self, axis=0, skipna=True, **kw):
        return self._reduce('any', axis)

    def all(self, axis=0, skipna=True, **kw):
        return self._reduce('all', axis)

    def mode(self, axis=0, numeric_only=False, dropna=True):
        cols = {}
        L = 0
        ms = []
        for j in range(len(self._vals)):
            m = self._col(j).mode(dropna)._v
            ms.append(m)
            L = max(L, len(m))
        for j, m in enumerate(ms):
            cols[self._cols._v[j]] = m + [nan] * (L - len(m))
        return DataFrame(cols)

    def idxmax(self, axis=0, skipna=True, **kw):
        if axis in (1, 'columns'):
            res = []
            for i in range(len(self._idx)):
                best = None
                for j in range(len(self._vals)):
                    x = self._vals[j][i]
                    if x == x and (best is None or x > best[1]):
                        best = (self._cols._v[j], x)
                res.append(best[0] if best else nan)
            return Series(res, self._idx)
        vals = [self._col(j).idxmax() for j in range(len(self._vals)) if self._dts[j] in _NUMDT]
        names = [self._cols._v[j] for j in range(len(self._vals)) if self._dts[j] in _NUMDT]
        return Series(vals, Index(names))

    def idxmin(self, axis=0, skipna=True, **kw):
        if axis in (1, 'columns'):
            res = []
            for i in range(len(self._idx)):
                best = None
                for j in range(len(self._vals)):
                    x = self._vals[j][i]
                    if x == x and (best is None or x < best[1]):
                        best = (self._cols._v[j], x)
                res.append(best[0] if best else nan)
            return Series(res, self._idx)
        vals = [self._col(j).idxmin() for j in range(len(self._vals)) if self._dts[j] in _NUMDT]
        names = [self._cols._v[j] for j in range(len(self._vals)) if self._dts[j] in _NUMDT]
        return Series(vals, Index(names))

    def quantile(self, q=0.5, axis=0, numeric_only=True, interpolation='linear'):
        qs = q if isinstance(q, (list, tuple)) else [q]
        cols = {}
        for j in range(len(self._vals)):
            if self._dts[j] not in _NUMDT:
                continue
            s = self._col(j)
            cols[self._cols._v[j]] = [s.quantile(x) for x in qs]
        if isinstance(q, (list, tuple)):
            return DataFrame(cols, index=Index([float(x) for x in qs]))
        return Series([v[0] for v in cols.values()], Index(list(cols.keys())), name=q)

    def describe(self, percentiles=None, include=None, exclude=None):
        numcols = [j for j in range(len(self._vals)) if self._dts[j] in ('int64', 'float64')]
        objcols = [j for j in range(len(self._vals)) if self._dts[j] not in ('int64', 'float64')]
        if include == 'all':
            sel = list(range(len(self._vals)))
        elif include is not None:
            inc = include if isinstance(include, (list, tuple)) else [include]
            sel = []
            for j in range(len(self._vals)):
                dt = self._dts[j]
                for s in inc:
                    if (s == 'number' and dt in ('int64', 'float64')) or (s in ('object', 'str', 'string', object, str) and dt in ('object', 'str')) or norm_dtype(s) == dt:
                        sel.append(j)
                        break
        elif numcols:
            sel = numcols
        else:
            sel = objcols
        ds = {}
        for j in sel:
            ds[self._cols._v[j]] = self._col(j).describe(percentiles)
        labs = []
        for d in ds.values():
            for k in d._idx._v:
                if k not in labs:
                    labs.append(k)
        if include == 'all' and numcols and objcols:
            order = ['count', 'unique', 'top', 'freq', 'mean', 'std', 'min']
            labs = [k for k in order if k in labs] + [k for k in labs if k not in order]
            mid = [k for k in labs if k not in ('count', 'unique', 'top', 'freq', 'mean', 'std', 'min', 'max')]
            labs = ['count', 'unique', 'top', 'freq', 'mean', 'std', 'min'] + mid + ['max']
        cols = {}
        for c, d in ds.items():
            p = d._idx._mkpos()
            cols[c] = [d._v[p[k]] if k in p else nan for k in labs]
        r = DataFrame(cols, index=Index(labs))
        for j in range(len(r._vals)):
            if r._dts[j] == 'float64' or True:
                pass
        return r

    def corr(self, method='pearson', min_periods=1, numeric_only=False):
        js = [j for j in range(len(self._vals)) if self._dts[j] in ('int64', 'float64', 'bool')]
        names = [self._cols._v[j] for j in js]
        cols = {}
        for a in js:
            sa = self._col(a)
            cols[self._cols._v[a]] = [sa.corr(self._col(b), method) for b in js]
        return DataFrame(cols, index=Index(names))

    def cov(self, min_periods=None, ddof=1, numeric_only=False):
        js = [j for j in range(len(self._vals)) if self._dts[j] in ('int64', 'float64', 'bool')]
        names = [self._cols._v[j] for j in js]
        cols = {}
        for a in js:
            sa = self._col(a)
            cols[self._cols._v[a]] = [sa.cov(self._col(b), ddof=ddof) for b in js]
        return DataFrame(cols, index=Index(names))

    def corrwith(self, other, axis=0):
        out = []
        names = []
        for j in range(len(self._vals)):
            c = self._cols._v[j]
            if isinstance(other, Series):
                out.append(self._col(j).corr(other))
                names.append(c)
            elif c in other._cols:
                out.append(self._col(j).corr(other[c]))
                names.append(c)
        return Series(out, Index(names))

    def value_counts(self, subset=None, normalize=False, sort=True, ascending=False, dropna=True):
        cols = subset if subset is not None else list(self._cols._v)
        if not isinstance(cols, list):
            cols = [cols]
        arrs = [self._vals[self._colpos(c)] for c in cols]
        cnt = {}
        order = []
        for t in zip(*arrs):
            if t in cnt:
                cnt[t] += 1
            else:
                cnt[t] = 1
                order.append(t)
        items = [(k, cnt[k]) for k in order]
        if sort:
            items = sorted(items, key=lambda kv: kv[1], reverse=not ascending)
        tot = len(self._idx)
        ix = MultiIndex(_tuples=[k for k, c in items], names=cols) if len(cols) > 1 else Index([k[0] for k, c in items], name=cols[0])
        if normalize:
            return Series([c / tot for k, c in items], ix, name='proportion')
        return Series([c for k, c in items], ix, name='count')

    # ------------------------------------------------------------ cumulative
    def cumsum(self, axis=0, skipna=True, **kw):
        return self._mapcols(lambda s: s.cumsum(), True)

    def cumprod(self, axis=0, skipna=True, **kw):
        return self._mapcols(lambda s: s.cumprod(), True)

    def cummax(self, axis=0, skipna=True, **kw):
        return self._mapcols(lambda s: s.cummax(), True)

    def cummin(self, axis=0, skipna=True, **kw):
        return self._mapcols(lambda s: s.cummin(), True)

    def diff(self, periods=1, axis=0):
        return self._mapcols(lambda s: s.diff(periods), True)

    def pct_change(self, periods=1, **kw):
        return self._mapcols(lambda s: s.pct_change(periods), True)

    def shift(self, periods=1, freq=None, axis=0, fill_value=nan):
        return self._mapcols(lambda s: s.shift(periods, fill_value=fill_value))

    def rank(self, **kw):
        return self._mapcols(lambda s: s.rank(**kw), True)

    def rolling(self, window, min_periods=None, center=False, **kw):
        from .window import Rolling
        return Rolling(self, window, min_periods, center)

    def expanding(self, min_periods=1, **kw):
        from .window import Expanding
        return Expanding(self, min_periods)

    def ewm(self, com=None, span=None, halflife=None, alpha=None, adjust=True, min_periods=0, **kw):
        from .window import EWM
        return EWM(self, com, span, halflife, alpha, adjust, min_periods)

    # ------------------------------------------------------------ sorting / duplicates
    def sort_values(self, by=None, axis=0, ascending=True, inplace=False, kind='quicksort', na_position='last', ignore_index=False, key=None):
        if axis in (1, 'columns'):
            raise NotImplementedError('sort_values axis=1')
        bl = by if isinstance(by, list) else [by]
        asc = ascending if isinstance(ascending, list) else [ascending] * len(bl)
        n = len(self._idx)
        order = list(range(n))
        for k in range(len(bl) - 1, -1, -1):
            kv = self._vals[self._colpos(bl[k])]
            if key is not None:
                kv = key(Series(kv, self._idx))._v
            good = [i for i in order if not (kv[i] is None or kv[i] != kv[i])]
            bad = [i for i in order if (kv[i] is None or kv[i] != kv[i])]
            good = sorted(good, key=lambda i: kv[i], reverse=not asc[k])
            order = good + bad if na_position == 'last' else bad + good
        r = self._take_rows(order)
        if ignore_index:
            r._idx = RangeIndex(n)
        if inplace:
            self._vals, self._dts, self._idx = r._vals, r._dts, r._idx
            return None
        return r

    def sort_index(self, axis=0, level=None, ascending=True, inplace=False, kind='quicksort', na_position='last', sort_remaining=True, ignore_index=False, key=None):
        if axis in (1, 'columns'):
            cv = self._cols._v
            order = sorted(range(len(cv)), key=lambda i: cv[i], reverse=not ascending)
            return self._take_cols(order)
        iv = self._idx._v
        if level is not None and self._idx.nlevels > 1:
            lv = level if isinstance(level, int) else self._idx.names.index(level)
            order = sorted(range(len(iv)), key=lambda i: iv[i][lv], reverse=not ascending)
        else:
            order = sorted(range(len(iv)), key=lambda i: iv[i], reverse=not ascending)
        r = self._take_rows(order)
        if ignore_index:
            r._idx = RangeIndex(len(iv))
        if inplace:
            self._vals, self._dts, self._idx = r._vals, r._dts, r._idx
            return None
        return r

    def _rowkeys(self, subset):
        cols = subset if subset is not None else list(self._cols._v)
        if not isinstance(cols, list):
            cols = [cols]
        arrs = [self._vals[self._colpos(c)] for c in cols]
        return list(zip(*arrs)) if len(arrs) > 1 else [(x,) for x in arrs[0]]

    def duplicated(self, subset=None, keep='first'):
        keys = self._rowkeys(subset)
        seen = {}
        res = []
        if keep == 'first':
            for k in keys:
                res.append(k in seen)
                seen[k] = 1
        elif keep == 'last':
            for k in keys[::-1]:
                res.append(k in seen)
                seen[k] = 1
            res = res[::-1]
        else:
            cnt = {}
            for k in keys:
                cnt[k] = cnt.get(k, 0) + 1
            res = [cnt[k] > 1 for k in keys]
        return Series(res, self._idx, _dt='bool')

    def drop_duplicates(self, subset=None, keep='first', inplace=False, ignore_index=False):
        d = self.duplicated(subset, keep)._v
        r = self._take_rows([i for i in range(len(d)) if not d[i]])
        if ignore_index:
            r._idx = RangeIndex(len(r._idx))
        if inplace:
            self._vals, self._dts, self._idx = r._vals, r._dts, r._idx
            return None
        return r

    def nlargest(self, n, columns, keep='first'):
        cl = columns if isinstance(columns, list) else [columns]
        return self.sort_values(cl, ascending=False).head(n)

    def nsmallest(self, n, columns, keep='first'):
        cl = columns if isinstance(columns, list) else [columns]
        return self.sort_values(cl, ascending=True).head(n)

    def sample(self, n=None, frac=None, replace=False, random_state=None, axis=0, **kw):
        import random
        if random_state is not None:
            random.seed(random_state)
        L = len(self._idx)
        if n is None:
            n = int(round(frac * L))
        if replace:
            pos = [random.randrange(L) for _ in range(n)]
        else:
            pos = random.sample(list(range(L)), n)
        return self._take_rows(pos)

    def equals(self, other):
        return isinstance(other, DataFrame) and self._vals == other._vals and self._cols._v == other._cols._v and self._idx._v == other._idx._v

    def transpose(self, *a, **k):
        rows = list(zip(*self._vals)) if self._vals else []
        vals = []
        dts = []
        for r in rows:
            v, dt = infer(list(r))
            vals.append(v)
            dts.append(dt)
        if not rows and len(self._idx) == 0:
            vals = []
        if self._cols.nlevels > 1:
            nidx = MultiIndex(_tuples=list(self._cols._v), names=self._cols.names)
        else:
            nidx = Index(list(self._cols._v), name=self._cols.name)
        return DataFrame._fast(vals, dts, self._idx, nidx)

    def squeeze(self, axis=None):
        if len(self._vals) == 1:
            s = self._col(0)
            return s.squeeze()
        return self

    def memory_usage(self, index=True, deep=False):
        return Series([8 * len(self._idx)] * len(self._vals), self._cols)

    def info(self, buf=None, **kw):
        import sys
        out = buf if buf is not None else sys.stdout
        n = len(self._idx)
        lines = ["<class 'pandas.DataFrame'>"]
        if isinstance(self._idx, RangeIndex):
            lines.append('RangeIndex: %d entries, %s to %s' % (n, self._idx._v[0] if n else 0, self._idx._v[-1] if n else -1))
        else:
            lines.append('Index: %d entries, %s to %s' % (n, self._idx._v[0] if n else '', self._idx._v[-1] if n else ''))
        m = len(self._vals)
        lines.append('Data columns (total %d columns):' % m)
        names = [str(c) for c in self._cols._v]
        w = max([6] + [len(c) for c in names])
        lines.append(' #   ' + 'Column'.ljust(w) + '  Non-Null Count  Dtype  ')
        lines.append('---  ' + '-' * w + '  --------------  -----  ')
        counts = {}
        for j in range(m):
            nn = _lred('count', self._vals[j])
            cs = '%d non-null' % nn
            lines.append((' %d' % j).ljust(4) + ' ' + names[j].ljust(w) + '  ' + cs.ljust(14) + '  ' + self._dts[j].ljust(7))
            counts[self._dts[j]] = counts.get(self._dts[j], 0) + 1
        lines.append('dtypes: ' + ', '.join(['%s(%d)' % (k, counts[k]) for k in sorted(counts.keys())]))
        mem = 132 if isinstance(self._idx, RangeIndex) else 8 * n
        for dt in self._dts:
            mem += n * (1 if dt == 'bool' else 8)
        lines.append('memory usage: %.1f bytes' % mem)
        out.write('\n'.join(lines) + '\n')

    # ------------------------------------------------------------ query / eval
    def query(self, expr, inplace=False, local_dict=None, **kw):
        from .strs import eval_query
        mask = eval_query(self, expr, local_dict)
        r = self[mask]
        if inplace:
            self._vals, self._dts, self._idx = r._vals, r._dts, r._idx
            return None
        return r

    def eval(self, expr, inplace=False, **kw):
        from .strs import eval_query
        return eval_query(self, expr, None)

    # ------------------------------------------------------------ combine
    def groupby(self, by=None, axis=0, level=None, as_index=True, sort=True, group_keys=True, observed=False, dropna=True):
        from .groupby import DataFrameGroupBy
        return DataFrameGroupBy(self, by, level, as_index, sort, dropna)

    def merge(self, right, how='inner', on=None, left_on=None, right_on=None, left_index=False, right_index=False, sort=False, suffixes=('_x', '_y'), indicator=False, validate=None):
        from .reshape import merge
        return merge(self, right, how, on, left_on, right_on, left_index, right_index, sort, suffixes, indicator)

    def join(self, other, on=None, how='left', lsuffix='', rsuffix='', sort=False):
        from .reshape import merge
        if isinstance(other, Series):
            other = other.to_frame()
        if on is None:
            return merge(self, other, how, None, None, None, True, True, sort, (lsuffix, rsuffix), False)
        return merge(self, other, how, None, on, None, False, True, sort, (lsuffix, rsuffix), False)

    def pivot_table(self, values=None, index=None, columns=None, aggfunc='mean', fill_value=None, margins=False, dropna=True, margins_name='All', observed=False, sort=True):
        from .reshape import pivot_table
        return pivot_table(self, values, index, columns, aggfunc, fill_value, margins, dropna, margins_name)

    def pivot(self, columns, index=None, values=None):
        from .reshape import pivot
        return pivot(self, index, columns, values)

    def melt(self, id_vars=None, value_vars=None, var_name=None, value_name='value', ignore_index=True):
        from .reshape import melt
        return melt(self, id_vars, value_vars, var_name, value_name)

    def stack(self, level=-1, dropna=True, **kw):
        from .reshape import stack
        return stack(self)

    def unstack(self, level=-1, fill_value=None):
        from .reshape import unstack
        return unstack(self, level)

    def explode(self, column, ignore_index=False):
        idx = []
        rows = []
        j = self._colpos(column)
        for i in range(len(self._idx)):
            x = self._vals[j][i]
            items = x if isinstance(x, (list, tuple)) else [x]
            for y in items:
                idx.append(self._idx._v[i])
                r = [v[i] for v in self._vals]
                r[j] = y
                rows.append(r)
        out = DataFrame(rows, columns=self._cols)
        out._idx = RangeIndex(len(rows)) if ignore_index else Index(idx)
        return out

    def append(self, other, ignore_index=False):
        from .reshape import concat
        return concat([self, other], ignore_index=ignore_index)

    def first_valid_index(self):
        for i in range(len(self._idx)):
            if all([v[i] == v[i] for v in self._vals]):
                return self._idx._v[i]
        return None

    def last_valid_index(self):
        for i in range(len(self._idx) - 1, -1, -1):
            if all([v[i] == v[i] for v in self._vals]):
                return self._idx._v[i]
        return None

    def update(self, other, overwrite=True):
        for c in other._cols._v:
            if c in self._cols:
                s = self[c]
                o = other[c]
                p = o._idx._mkpos()
                v = [o._v[p[k]] if (k in p and o._v[p[k]] == o._v[p[k]]) else x for k, x in zip(self._idx._v, s._v)]
                self[c] = v

    def combine_first(self, other):
        idx = self._idx.union(other._idx)
        cols = self._cols.union(other._cols)
        a = self.reindex(index=idx, columns=cols)
        b = other.reindex(index=idx, columns=cols)
        vals = []
        for j in range(len(cols)):
            v, dt = infer([y if x != x else x for x, y in zip(a._vals[j], b._vals[j])])
            vals.append(v)
        return DataFrame._fast(vals, [infer(v)[1] for v in vals], cols, idx)

    def to_markdown(self, *a, **k):
        cols = [str(c) for c in self._cols._v]
        rows = [[str(x) for x in r] for r in zip(*([self._idx._v] + self._vals))]
        hdr = [''] + cols
        w = [max([len(hdr[k])] + [len(r[k]) for r in rows]) for k in range(len(hdr))]
        out = ['| ' + ' | '.join([hdr[k].ljust(w[k]) for k in range(len(hdr))]) + ' |']
        out.append('|' + '|'.join([':' + '-' * (w[k]) if True else '' for k in range(len(hdr))]) + '|')
        for r in rows:
            out.append('| ' + ' | '.join([r[k].ljust(w[k]) for k in range(len(hdr))]) + ' |')
        return '\n'.join(out)


def _col_from(value, idx):
    """value (Series / list-like / scalar) -> (list, dtype) aligned with idx"""
    n = len(idx)
    if isinstance(value, Series):
        if value._idx is idx or value._idx._v == idx._v:
            return list(value._v), value._dt
        p = value._idx._mkpos()
        v = [value._v[p[k]] if k in p else nan for k in idx._v]
        v, dt = infer(v)
        return v, dt
    if isinstance(value, dict):
        v = [value.get(k, nan) for k in idx._v]
        return infer(v)
    if _is_listlike(value):
        v = list(_lst(value))
        if len(v) != n:
            raise ValueError('Length of values (%d) does not match length of index (%d)' % (len(v), n))
        return infer(v)
    return infer([value] * n)


def _col_from_empty(value, df):
    if _is_listlike(value) and not isinstance(value, Series):
        v = list(_lst(value))
        df._idx = RangeIndex(len(v))
        return infer(v)
    if isinstance(value, Series):
        df._idx = value._idx
        return list(value._v), value._dt
    return infer([value] * len(df._idx))
