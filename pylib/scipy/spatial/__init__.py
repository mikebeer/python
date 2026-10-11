"""scipy.spatial subset: distance functions, cdist / pdist / squareform and a brute-force KDTree."""
import math
import numpy as np
from .._util import inf, nan, tolist
from . import distance


def distance_matrix(x, y, p=2, threshold=1000000):
    return distance.cdist(x, y, "minkowski", p=p)


def _pdist_one(a, b, p):
    if p == 2:
        s = 0.0
        for i in range(len(a)):
            d = a[i] - b[i]
            s += d * d
        return math.sqrt(s)
    if p == inf:
        return max([abs(a[i] - b[i]) for i in range(len(a))])
    if p == 1:
        return sum([abs(a[i] - b[i]) for i in range(len(a))])
    return sum([abs(a[i] - b[i]) ** p for i in range(len(a))]) ** (1.0 / p)


class KDTree:
    """nearest neighbour queries by exhaustive search (same results as scipy's KDTree)"""

    def __init__(self, data, leafsize=10, compact_nodes=True, copy_data=False, balanced_tree=True, boxsize=None):
        a = np.asarray(data) * 1.0
        if a.ndim == 1:
            a = a.reshape((-1, 1)) if False else a.reshape((a.shape[0], 1))
        self.data = a
        self._rows = [[float(v) for v in r] for r in a.tolist()]
        self.n = a.shape[0]
        self.m = a.shape[1]
        self.size = self.n

    def _query_one(self, x, k, p, ub, eps):
        ds = []
        for i in range(self.n):
            d = _pdist_one(self._rows[i], x, p)
            if d <= ub:
                ds.append((d, i))
        ds.sort()
        return ds[:k]

    def query(self, x, k=1, eps=0.0, p=2.0, distance_upper_bound=inf, workers=1):
        xa = np.asarray(x) * 1.0
        ks = [k] if isinstance(k, int) else list(k)
        single = xa.ndim == 1
        pts = [tolist(xa)] if single else [[float(v) for v in r] for r in xa.reshape((-1, self.m)).tolist()]
        dres = []
        ires = []
        for pt in pts:
            res = self._query_one(pt, max(ks), p, distance_upper_bound, eps)
            dr = []
            ir = []
            for kk in ks:
                if kk - 1 < len(res):
                    dr.append(res[kk - 1][0])
                    ir.append(res[kk - 1][1])
                else:
                    dr.append(inf)
                    ir.append(self.n)
            if isinstance(k, int) and k == 1:
                dres.append(dr[0])
                ires.append(ir[0])
            elif isinstance(k, int):
                dr = []
                ir = []
                for j in range(k):
                    if j < len(res):
                        dr.append(res[j][0])
                        ir.append(res[j][1])
                    else:
                        dr.append(inf)
                        ir.append(self.n)
                dres.append(dr)
                ires.append(ir)
            else:
                dres.append(dr)
                ires.append(ir)
        if single:
            return (dres[0], ires[0]) if not isinstance(dres[0], list) else (np.array(dres[0]), np.array(ires[0]))
        if xa.ndim > 2:
            raise NotImplementedError("query with more than 2 dimensions")
        return np.array(dres), np.array(ires)

    def query_ball_point(self, x, r, p=2.0, eps=0.0, workers=1, return_sorted=None, return_length=False):
        xa = np.asarray(x) * 1.0
        single = xa.ndim == 1
        pts = [tolist(xa)] if single else [[float(v) for v in rr] for rr in xa.tolist()]
        out = []
        for pt in pts:
            idx = [i for i in range(self.n) if _pdist_one(self._rows[i], pt, p) <= r]
            out.append(len(idx) if return_length else idx)
        if single:
            return out[0]
        if return_length:
            return np.array(out)
        return out

    def query_pairs(self, r, p=2.0, eps=0.0, output_type="set"):
        out = set()
        for i in range(self.n):
            for j in range(i + 1, self.n):
                if _pdist_one(self._rows[i], self._rows[j], p) <= r:
                    out.add((i, j))
        if output_type == "ndarray":
            return np.array(sorted(out))
        return out

    def count_neighbors(self, other, r, p=2.0):
        tot = 0
        for a in self._rows:
            for b in other._rows:
                if _pdist_one(a, b, p) <= r:
                    tot += 1
        return tot


cKDTree = KDTree
