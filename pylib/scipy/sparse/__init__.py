"""scipy.sparse subset: csr_matrix / csc_matrix / coo_matrix (small, list based) and helper constructors."""
import math
import numpy as np
from .._util import inf, nan, tolist
from . import linalg


def _is_dense_like(x):
    return isinstance(x, (list, tuple, np.ndarray))


class spmatrix:
    format = "coo"
    _name = "Coordinate"

    # ---- construction (shared) ----
    def __init__(self, arg1, shape=None, dtype=None, copy=False):
        self._shape = (0, 0)
        self._t = []  # list of (row, col, value)
        if isinstance(arg1, spmatrix):
            self._shape = arg1.shape
            self._t = list(arg1._triples())
        elif isinstance(arg1, tuple) and len(arg1) == 2 and all([isinstance(v, int) for v in arg1]) and \
                not _is_dense_like(arg1[0]):
            self._shape = (arg1[0], arg1[1])
        elif isinstance(arg1, tuple) and len(arg1) == 2 and isinstance(arg1[1], tuple):
            data = np.asarray(arg1[0]).flatten().tolist()
            row = np.asarray(arg1[1][0]).flatten().tolist()
            col = np.asarray(arg1[1][1]).flatten().tolist()
            self._t = [(int(row[i]), int(col[i]), data[i]) for i in range(len(data))]
            if shape is None:
                shape = (max(row) + 1 if row else 0, max(col) + 1 if col else 0)
            self._shape = (int(shape[0]), int(shape[1]))
        elif isinstance(arg1, tuple) and len(arg1) == 3:
            data = np.asarray(arg1[0]).flatten().tolist()
            ind = np.asarray(arg1[1]).flatten().tolist()
            ptr = np.asarray(arg1[2]).flatten().tolist()
            t = []
            for r in range(len(ptr) - 1):
                for k in range(ptr[r], ptr[r + 1]):
                    if self.format == "csc":
                        t.append((ind[k], r, data[k]))
                    else:
                        t.append((r, ind[k], data[k]))
            self._t = t
            if shape is None:
                n = len(ptr) - 1
                other = (max(ind) + 1) if ind else 0
                shape = (other, n) if self.format == "csc" else (n, other)
            self._shape = (int(shape[0]), int(shape[1]))
        else:
            a = np.asarray(arg1)
            if a.ndim == 1:
                a = a.reshape((1, a.shape[0]))
            if a.ndim != 2:
                raise ValueError("expected dimension <= 2 array or matrix")
            rows = a.tolist()
            self._shape = (len(rows), len(rows[0]) if rows else 0)
            for i in range(len(rows)):
                for j in range(len(rows[i])):
                    if rows[i][j] != 0:
                        self._t.append((i, j, rows[i][j]))
        if dtype is not None:
            conv = float if str(dtype).find("float") >= 0 else (int if str(dtype).find("int") >= 0 else None)
            if conv is not None:
                self._t = [(i, j, conv(v)) for (i, j, v) in self._t]
        self._canon()

    def _canon(self):
        """merge duplicates and order according to the format"""
        d = {}
        order = []
        for (i, j, v) in self._t:
            k = (i, j)
            if k in d:
                d[k] = d[k] + v
            else:
                d[k] = v
                order.append(k)
        if self.format == "csr":
            order.sort()
        elif self.format == "csc":
            order.sort(key=lambda k: (k[1], k[0]))
        if self.format == "coo":
            self._t = [(i, j, v) for (i, j, v) in self._t]
        else:
            self._t = [(k[0], k[1], d[k]) for k in order]

    def _triples(self):
        return self._t

    def _new(self, triples, shape=None):
        m = self.__class__((0, 0))
        m._shape = self._shape if shape is None else shape
        m._t = list(triples)
        m._canon()
        return m

    # ---- properties ----
    @property
    def shape(self):
        return self._shape

    @property
    def ndim(self):
        return 2

    @property
    def nnz(self):
        return len(self._t)

    def getnnz(self, axis=None):
        return len(self._t)

    def count_nonzero(self):
        return len([1 for (i, j, v) in self._t if v != 0])

    @property
    def dtype(self):
        for (i, j, v) in self._t:
            if isinstance(v, float):
                return np.float64
            return np.int64
        return np.float64

    @property
    def data(self):
        return np.array([v for (i, j, v) in self._t])

    @property
    def row(self):
        return np.array([i for (i, j, v) in self._t])

    @property
    def col(self):
        return np.array([j for (i, j, v) in self._t])

    @property
    def indices(self):
        if self.format == "csc":
            return np.array([i for (i, j, v) in self._t])
        return np.array([j for (i, j, v) in self._t])

    @property
    def indptr(self):
        if self.format == "csc":
            n = self._shape[1]
            key = 1
        else:
            n = self._shape[0]
            key = 0
        ptr = [0] * (n + 1)
        for t in self._t:
            ptr[t[key] + 1] += 1
        for i in range(n):
            ptr[i + 1] += ptr[i]
        return np.array(ptr)

    @property
    def T(self):
        return self.transpose()

    def transpose(self, axes=None, copy=False):
        cls = {"csr": csc_matrix, "csc": csr_matrix, "coo": coo_matrix}[self.format]
        m = cls((0, 0))
        m._shape = (self._shape[1], self._shape[0])
        m._t = [(j, i, v) for (i, j, v) in self._t]
        m._canon()
        return m

    def copy(self):
        return self._new(self._t)

    def astype(self, dtype, casting="unsafe", copy=True):
        conv = float if str(dtype).find("float") >= 0 else int
        return self._new([(i, j, conv(v)) for (i, j, v) in self._t])

    # ---- conversion ----
    def toarray(self, order=None, out=None):
        r, c = self._shape
        zero = 0
        for (i, j, v) in self._t:
            if isinstance(v, float):
                zero = 0.0
                break
        out = [[zero] * c for i in range(r)]
        for (i, j, v) in self._t:
            out[i][j] += v
        return np.array(out) if r * c > 0 else np.zeros((r, c))

    def todense(self, order=None, out=None):
        return self.toarray()

    def tocsr(self, copy=False):
        m = csr_matrix((0, 0))
        m._shape = self._shape
        m._t = list(self._t)
        m._canon()
        return m

    def tocsc(self, copy=False):
        m = csc_matrix((0, 0))
        m._shape = self._shape
        m._t = list(self._t)
        m._canon()
        return m

    def tocoo(self, copy=False):
        m = coo_matrix((0, 0))
        m._shape = self._shape
        m._t = list(self._t)
        return m

    def asformat(self, fmt):
        return {"csr": self.tocsr, "csc": self.tocsc, "coo": self.tocoo}[fmt]()

    def eliminate_zeros(self):
        self._t = [t for t in self._t if t[2] != 0]

    def sum_duplicates(self):
        self._canon()

    def nonzero(self):
        t = [x for x in self._t if x[2] != 0]
        return np.array([x[0] for x in t]), np.array([x[1] for x in t])

    # ---- arithmetic ----
    def dot(self, other):
        return self._matmul(other)

    def _matmul(self, other):
        if isinstance(other, spmatrix):
            if self._shape[1] != other._shape[0]:
                raise ValueError("dimension mismatch")
            rows = {}
            for (j, k, v) in other._t:
                rows.setdefault(j, []).append((k, v))
            acc = {}
            order = []
            for (i, j, v) in self._t:
                for (k, w) in rows.get(j, []):
                    key = (i, k)
                    if key in acc:
                        acc[key] += v * w
                    else:
                        acc[key] = v * w
                        order.append(key)
            m = self._new([(k[0], k[1], acc[k]) for k in order], (self._shape[0], other._shape[1]))
            return m
        o = np.asarray(other)
        if o.ndim == 0:
            return self._scale(o.tolist())
        if o.ndim == 1:
            if o.shape[0] != self._shape[1]:
                raise ValueError("dimension mismatch")
            ov = o.tolist()
            res = [0 * ov[0]] * self._shape[0] if ov else [0.0] * self._shape[0]
            res = list(res)
            for (i, j, v) in self._t:
                res[i] = res[i] + v * ov[j]
            return np.array(res)
        if o.shape[0] != self._shape[1]:
            raise ValueError("dimension mismatch")
        ov = o.tolist()
        nc = o.shape[1]
        res = [[0 * ov[0][0]] * nc for i in range(self._shape[0])]
        res = [list(r) for r in res]
        for (i, j, v) in self._t:
            row = ov[j]
            for k in range(nc):
                res[i][k] = res[i][k] + v * row[k]
        return np.array(res)

    def __matmul__(self, other):
        if isinstance(other, (int, float)):
            raise ValueError("Scalar operands are not allowed, use '*' instead")
        return self._matmul(other)

    def __rmatmul__(self, other):
        o = np.asarray(other)
        return self.T._matmul(o.T).T if o.ndim == 2 else self.T._matmul(o)

    def _scale(self, s):
        return self._new([(i, j, v * s) for (i, j, v) in self._t])

    def __mul__(self, other):
        if isinstance(other, (int, float)):
            return self._scale(other)
        return self._matmul(other)

    def __rmul__(self, other):
        if isinstance(other, (int, float)):
            return self._scale(other)
        o = np.asarray(other)
        return self.T._matmul(o.T).T if o.ndim == 2 else self.T._matmul(o)

    def __truediv__(self, other):
        if isinstance(other, (int, float)):
            return self._new([(i, j, v / other) for (i, j, v) in self._t])
        raise NotImplementedError("sparse / non-scalar")

    def __neg__(self):
        return self._scale(-1)

    def __pos__(self):
        return self

    def __abs__(self):
        return self._new([(i, j, abs(v)) for (i, j, v) in self._t])

    def _combine(self, other, sign):
        if isinstance(other, spmatrix):
            if other._shape != self._shape:
                raise ValueError("inconsistent shapes")
            r = self._new(list(self._t) + [(i, j, sign * v) for (i, j, v) in other._t])
            r._t = [t for t in r._t if t[2] != 0]
            return r
        if isinstance(other, (int, float)):
            if other == 0:
                return self.copy()
            raise NotImplementedError("adding a nonzero scalar to a sparse matrix is not supported")
        d = np.asarray(other)
        return self.toarray() + d if sign > 0 else self.toarray() - d

    def __add__(self, other):
        return self._combine(other, 1)

    def __radd__(self, other):
        return self._combine(other, 1)

    def __sub__(self, other):
        return self._combine(other, -1)

    def __rsub__(self, other):
        if isinstance(other, spmatrix):
            return other._combine(self, -1)
        return np.asarray(other) - self.toarray()

    def multiply(self, other):
        if isinstance(other, (int, float)):
            return self._scale(other)
        if isinstance(other, spmatrix):
            d = {}
            for (i, j, v) in other._t:
                d[(i, j)] = d.get((i, j), 0) + v
            return self._new([(i, j, v * d[(i, j)]) for (i, j, v) in self._t if (i, j) in d])
        o = np.asarray(other).tolist()
        return self._new([(i, j, v * o[i][j]) for (i, j, v) in self._t])

    def power(self, n):
        return self._new([(i, j, v ** n) for (i, j, v) in self._t])

    def __pow__(self, n):
        if self._shape[0] != self._shape[1]:
            raise TypeError("matrix is not square")
        r = identity(self._shape[0], format=self.format)
        for k in range(n):
            r = r._matmul(self)
        return r

    def __eq__(self, other):
        if isinstance(other, spmatrix):
            return self.toarray().tolist() == other.toarray().tolist() and self._shape == other._shape
        return NotImplemented

    # ---- reductions ----
    def sum(self, axis=None, dtype=None, out=None):
        if axis is None:
            s = 0
            for (i, j, v) in self._t:
                s = s + v
            return s
        if axis < 0:
            axis += 2
        n = self._shape[1] if axis == 0 else self._shape[0]
        res = [0] * n
        for (i, j, v) in self._t:
            k = j if axis == 0 else i
            res[k] = res[k] + v
        r = np.array(res)
        return r.reshape((1, n)) if axis == 0 else r.reshape((n, 1))

    def mean(self, axis=None, dtype=None, out=None):
        if axis is None:
            return self.sum() / (self._shape[0] * self._shape[1])
        s = self.sum(axis)
        return s / (self._shape[0] if axis in (0, -2) else self._shape[1])

    def diagonal(self, k=0):
        n = min(self._shape[0] - max(-k, 0), self._shape[1] - max(k, 0))
        res = [0] * max(n, 0)
        for (i, j, v) in self._t:
            if j - i == k:
                res[i - max(-k, 0)] = res[i - max(-k, 0)] + v
        return np.array(res)

    def trace(self, offset=0):
        return sum(self.diagonal(offset).tolist())

    def max(self, axis=None):
        a = self.toarray()
        return np.max(a) if axis is None else np.max(a, axis=axis)

    def min(self, axis=None):
        a = self.toarray()
        return np.min(a) if axis is None else np.min(a, axis=axis)

    # ---- indexing ----
    def _get(self, i, j):
        s = 0
        found = False
        for (a, b, v) in self._t:
            if a == i and b == j:
                s = s + v
                found = True
        if not found:
            for (a, b, v) in self._t:
                if isinstance(v, float):
                    return 0.0
            return 0
        return s

    def getrow(self, i):
        return csr_matrix((0, 0))._from(self, [(0, j, v) for (a, j, v) in self._t if a == i],
                                                     (1, self._shape[1]))

    def getcol(self, j):
        return csc_matrix((0, 0))._from(self, [(i, 0, v) for (i, b, v) in self._t if b == j],
                                                     (self._shape[0], 1))

    def _from(self, src, triples, shape):
        self._shape = shape
        self._t = list(triples)
        self._canon()
        return self

    def __getitem__(self, key):
        if isinstance(key, tuple) and len(key) == 2:
            a, b = key
        else:
            a, b = key, slice(None)
        r, c = self._shape
        if isinstance(a, int) and isinstance(b, int):
            if a < 0:
                a += r
            if b < 0:
                b += c
            if not (0 <= a < r and 0 <= b < c):
                raise IndexError("index out of bounds")
            return self._get(a, b)

        def idx(s, n):
            if isinstance(s, int):
                return [s + n if s < 0 else s]
            if isinstance(s, slice):
                return list(range(*s.indices(n)))
            return [int(v) for v in np.asarray(s).flatten().tolist()]
        ri = idx(a, r)
        ci = idx(b, c)
        rmap = {}
        for k in range(len(ri)):
            rmap.setdefault(ri[k], []).append(k)
        cmap = {}
        for k in range(len(ci)):
            cmap.setdefault(ci[k], []).append(k)
        t = []
        for (i, j, v) in self._t:
            if i in rmap and j in cmap:
                for p in rmap[i]:
                    for q in cmap[j]:
                        t.append((p, q, v))
        m = csr_matrix((0, 0)) if self.format != "csc" else csc_matrix((0, 0))
        return m._from(self, t, (len(ri), len(ci)))

    def __setitem__(self, key, value):
        if not (isinstance(key, tuple) and len(key) == 2 and isinstance(key[0], int) and isinstance(key[1], int)):
            raise NotImplementedError("only element assignment is supported")
        i, j = key
        self._t = [t for t in self._t if not (t[0] == i and t[1] == j)]
        if value != 0:
            self._t.append((i, j, value))
        self._canon()

    def __len__(self):
        raise TypeError("sparse matrix length is ambiguous; use getnnz() or shape[0]")

    def __iter__(self):
        for i in range(self._shape[0]):
            yield self.getrow(i)

    def __repr__(self):
        dt = "float64"
        if self._t and not isinstance(self._t[0][2], float):
            dt = "int64"
        return "<%s sparse matrix of dtype '%s'\n\twith %d stored elements and shape %s>" % (
            {"csr": "Compressed Sparse Row", "csc": "Compressed Sparse Column", "coo": "Coordinate"}[self.format],
            dt, len(self._t), str(self._shape))

    def __str__(self):
        return self.__repr__() + "\n  Coords\tValues\n" + "\n".join(["  (%d, %d)\t%s" % (i, j, repr(v)) for (i, j, v) in self._t])


class csr_matrix(spmatrix):
    format = "csr"


class csc_matrix(spmatrix):
    format = "csc"


class coo_matrix(spmatrix):
    format = "coo"


bsr_matrix = csr_matrix
lil_matrix = csr_matrix
dok_matrix = csr_matrix
csr_array = csr_matrix
csc_array = csc_matrix
coo_array = coo_matrix


def issparse(x):
    return isinstance(x, spmatrix)


isspmatrix = issparse
isspmatrix_csr = lambda x: isinstance(x, csr_matrix)
isspmatrix_csc = lambda x: isinstance(x, csc_matrix)
isspmatrix_coo = lambda x: isinstance(x, coo_matrix)


def _fmt(m, format):
    if format is None or format == "csr":
        return m.tocsr()
    if format == "csc":
        return m.tocsc()
    if format == "coo":
        return m.tocoo()
    return m.tocsr()


def eye(m, n=None, k=0, dtype=float, format=None):
    if n is None:
        n = m
    t = []
    for i in range(m):
        j = i + k
        if 0 <= j < n:
            t.append((i, j, 1.0))
    r = coo_matrix((0, 0))
    r._shape = (m, n)
    r._t = t
    return _fmt(r, format)


def identity(n, dtype="d", format=None):
    return eye(n, n, 0, dtype, format)


def diags(diagonals, offsets=0, shape=None, format=None, dtype=None):
    if not isinstance(offsets, (list, tuple)):
        offsets = [offsets]
        diagonals = [diagonals]
    else:
        diagonals = list(diagonals)
    dl = [np.asarray(d).flatten().tolist() for d in diagonals]
    if shape is None:
        m = len(dl[0]) + abs(offsets[0])
        shape = (m, m)
    t = []
    for d, off in zip(dl, offsets):
        for k in range(len(d) if len(d) > 1 else max(shape)):
            v = d[k] if len(d) > 1 else d[0]
            i = k + max(-off, 0)
            j = k + max(off, 0)
            if i < shape[0] and j < shape[1]:
                t.append((i, j, v))
    r = coo_matrix((0, 0))
    r._shape = (shape[0], shape[1])
    r._t = t
    return _fmt(r, format)


def spdiags(data, diags_, m=None, n=None, format=None):
    d = np.asarray(data).tolist()
    offs = [diags_] if isinstance(diags_, int) else list(diags_)
    t = []
    for row, off in zip(d, offs):
        for j in range(len(row)):
            i = j - off
            if 0 <= i < m and j < n and row[j] != 0:
                t.append((i, j, row[j]))
    r = coo_matrix((0, 0))
    r._shape = (m, n)
    r._t = t
    return _fmt(r, format)


def hstack(blocks, format=None, dtype=None):
    t = []
    off = 0
    rows = blocks[0].shape[0]
    for b in blocks:
        b = b if isinstance(b, spmatrix) else csr_matrix(b)
        for (i, j, v) in b._t:
            t.append((i, j + off, v))
        off += b.shape[1]
    r = coo_matrix((0, 0))
    r._shape = (rows, off)
    r._t = t
    return _fmt(r, format)


def vstack(blocks, format=None, dtype=None):
    t = []
    off = 0
    cols = blocks[0].shape[1]
    for b in blocks:
        b = b if isinstance(b, spmatrix) else csr_matrix(b)
        for (i, j, v) in b._t:
            t.append((i + off, j, v))
        off += b.shape[0]
    r = coo_matrix((0, 0))
    r._shape = (off, cols)
    r._t = t
    return _fmt(r, format)


def kron(A, B, format=None):
    A = A if isinstance(A, spmatrix) else csr_matrix(A)
    B = B if isinstance(B, spmatrix) else csr_matrix(B)
    t = []
    for (i, j, v) in A._t:
        for (k, l, w) in B._t:
            t.append((i * B.shape[0] + k, j * B.shape[1] + l, v * w))
    r = coo_matrix((0, 0))
    r._shape = (A.shape[0] * B.shape[0], A.shape[1] * B.shape[1])
    r._t = t
    return _fmt(r, format)


def block_diag(mats, format=None, dtype=None):
    t = []
    ro = 0
    co = 0
    for m in mats:
        m = m if isinstance(m, spmatrix) else csr_matrix(m)
        for (i, j, v) in m._t:
            t.append((i + ro, j + co, v))
        ro += m.shape[0]
        co += m.shape[1]
    r = coo_matrix((0, 0))
    r._shape = (ro, co)
    r._t = t
    return _fmt(r, format)


def tril(A, k=0, format=None):
    A = A if isinstance(A, spmatrix) else coo_matrix(A)
    r = coo_matrix((0, 0))
    r._shape = A.shape
    r._t = [(i, j, v) for (i, j, v) in A._t if j - i <= k]
    return _fmt(r, format)


def triu(A, k=0, format=None):
    A = A if isinstance(A, spmatrix) else coo_matrix(A)
    r = coo_matrix((0, 0))
    r._shape = A.shape
    r._t = [(i, j, v) for (i, j, v) in A._t if j - i >= k]
    return _fmt(r, format)


def random(m, n, density=0.01, format="coo", dtype=None, random_state=None, data_rvs=None):
    nnz = int(round(m * n * density))
    cells = np.random.permutation(m * n).tolist()[:nnz]
    vals = np.random.random(nnz).tolist() if nnz else []
    r = coo_matrix((0, 0))
    r._shape = (m, n)
    r._t = [(c // n, c % n, vals[k]) for k, c in enumerate(cells)]
    return _fmt(r, format)
