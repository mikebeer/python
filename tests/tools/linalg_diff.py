# Differential test of numpy.linalg: this interpreter against real numpy.
#
#   python3 tests/tools/linalg_diff.py compare ./c/py [N [SEED [MAXN]]]   N random cases on both
#   <interp> tests/tools/linalg_diff.py dump N SEED [MAXN]                 one JSON line per case
#
# 'dump' only uses the subset implemented here (LCG matrices, json), so it runs under ./c/py and
# under python3.  'compare' (python3 + numpy only) regenerates the results with numpy from the
# matrices in the dump and checks, with tolerances: eigvals/eig (sorted values, residuals,
# unit norm, LAPACK order), eigh/eigvalsh (values, residual, orthonormality), svd (values,
# reconstruction, orthonormality), qr (exact Q/R incl. signs), lstsq/pinv/solve/inv/det/
# matrix_rank/cond.  Exit status 1 when a hard check fails.
import sys
import json

SIZES = 10      # largest matrix order (the 'maxn' argument)


class Rng:
    def __init__(self, seed):
        self.s = seed % 2147483648

    def u(self):
        self.s = (self.s * 1103515245 + 12345) % 2147483648
        return self.s / 2147483648.0

    def pick(self, n):
        return int(self.u() * n) % n


def mk(m, n, f):
    return [[f(i, j) for j in range(n)] for i in range(m)]


def gen(rng, idx):
    """matrix (list of rows) for case idx; the kind cycles so that all kinds are covered"""
    kind = idx % 16
    n = 1 + rng.pick(SIZES)
    m = n
    if kind in (12, 13):
        m = 1 + rng.pick(SIZES)
    r = rng.u
    if kind == 0:
        a = mk(m, n, lambda i, j: r() * 4.0 - 2.0)
    elif kind == 1:
        a = mk(m, n, lambda i, j: float(round((r() * 4.0 - 2.0) * 2)))
    elif kind == 2:
        a = mk(m, n, lambda i, j: (r() * 4.0 - 2.0) if i >= j else 0.0)
    elif kind == 3:
        a = mk(m, n, lambda i, j: (r() * 4.0 - 2.0) if i <= j else 0.0)
    elif kind == 4:
        a = mk(m, n, lambda i, j: (r() * 4.0 - 2.0) if r() > 0.6 else 0.0)
    elif kind == 5:
        a = mk(m, n, lambda i, j: 0.0)
        for i in range(n):
            for j in range(i + 1):
                a[i][j] = a[j][i] = r() * 4.0 - 2.0
    elif kind == 6:
        a = mk(m, n, lambda i, j: 0.0)
        for i in range(n):
            for j in range(i + 1):
                a[i][j] = a[j][i] = float(round((r() * 2.0 - 1.0) * 6)) / 2.0
    elif kind == 7:
        # repeated eigenvalues, diagonal
        vals = [float(rng.pick(3)) for _ in range(n)]
        a = mk(m, n, lambda i, j: vals[i] if i == j else 0.0)
    elif kind == 8:
        # defective: Jordan blocks
        lam = float(rng.pick(3) - 1)
        a = mk(m, n, lambda i, j: lam if i == j else (1.0 if j == i + 1 and r() < 0.8 else 0.0))
    elif kind == 9:
        # permutation matrix
        p = list(range(n))
        for i in range(n - 1, 0, -1):
            j = rng.pick(i + 1)
            p[i], p[j] = p[j], p[i]
        a = mk(m, n, lambda i, j: 1.0 if p[i] == j else 0.0)
    elif kind == 10:
        # companion matrix of a polynomial with small integer roots (real, repeated)
        roots = [float(rng.pick(7) - 3) for _ in range(n)]
        c = [1.0]
        for rt in roots:
            d = c + [0.0]
            for k in range(len(c)):
                d[k + 1] -= rt * c[k]
            c = d
        a = mk(m, n, lambda i, j: 0.0)
        for j in range(n):
            a[0][j] = -c[j + 1]
        for i in range(1, n):
            a[i][i - 1] = 1.0
    elif kind == 11:
        # rank deficient square: product of n x k and k x n
        k = 1 + rng.pick(n)
        x = mk(n, k, lambda i, j: float(round(r() * 6 - 3)))
        y = mk(k, n, lambda i, j: float(round(r() * 6 - 3)))
        a = mk(n, n, lambda i, j: sum(x[i][t] * y[t][j] for t in range(k)))
    elif kind == 12:
        a = mk(m, n, lambda i, j: r() * 4.0 - 2.0)
    elif kind == 13:
        k = 1 + rng.pick(min(m, n))
        x = mk(m, k, lambda i, j: float(round(r() * 6 - 3)))
        y = mk(k, n, lambda i, j: float(round(r() * 6 - 3)))
        a = mk(m, n, lambda i, j: sum(x[i][t] * y[t][j] for t in range(k)))
    elif kind == 14:
        # badly scaled
        sc = [10.0 ** (rng.pick(9) - 4) for _ in range(n)]
        a = mk(m, n, lambda i, j: (r() * 4.0 - 2.0) * sc[i] / sc[j])
    else:
        # complex pairs: rotation blocks plus noise
        a = mk(m, n, lambda i, j: 0.0)
        i = 0
        while i < n:
            if i + 1 < n and r() < 0.7:
                p = r() * 2 - 1
                q = r() * 2 + 0.2
                a[i][i] = a[i + 1][i + 1] = p
                a[i][i + 1] = -q
                a[i + 1][i] = q
                i += 2
            else:
                a[i][i] = r() * 2 - 1
                i += 1
        for i in range(n):
            for j in range(n):
                if i != j and r() < 0.2:
                    a[i][j] += r() - 0.5
    return a


def enc(x):
    import numpy as np
    if isinstance(x, tuple):
        return [enc(t) for t in x]
    if isinstance(x, (int, float)):
        return float(x)
    x = np.asarray(x)
    if str(x.dtype).startswith('complex'):
        return {"re": x.real.tolist(), "im": x.imag.tolist()}
    return x.tolist()


def run_case(a):
    import numpy as np
    la = np.linalg
    out = {}
    m = len(a)
    n = len(a[0])
    A = np.array(a)

    def attempt(name, f):
        try:
            out[name] = enc(f())
        except Exception as e:
            out[name] = "ERR " + type(e).__name__
    if m == n:
        b = np.array([[float((i * 7 + j * 3) % 5) - 2.0 for j in range(2)] for i in range(n)])
        attempt("det", lambda: la.det(A))
        attempt("inv", lambda: la.inv(A))
        attempt("solve", lambda: la.solve(A, b))
        attempt("eigvals", lambda: la.eigvals(A))
        attempt("eig", lambda: la.eig(A))
        S = A + A.T
        attempt("eigh", lambda: la.eigh(S))
        attempt("eigvalsh", lambda: la.eigvalsh(S))
        attempt("eighU", lambda: la.eigh(S, UPLO='U'))
    attempt("svd", lambda: la.svd(A))
    attempt("svd_r", lambda: la.svd(A, full_matrices=False))
    attempt("svdvals", lambda: la.svd(A, compute_uv=False))
    attempt("qr", lambda: la.qr(A))
    attempt("qr_c", lambda: la.qr(A, mode='complete'))
    attempt("pinv", lambda: la.pinv(A))
    attempt("rank", lambda: la.matrix_rank(A))
    attempt("cond", lambda: la.cond(A))
    bb = np.array([float((i * 5) % 7) - 3.0 for i in range(m)])
    attempt("lstsq", lambda: la.lstsq(A, bb, rcond=None)[:3])
    return out


def dump(count, seed, maxn=None):
    global SIZES
    if maxn:
        SIZES = maxn
    rng = Rng(seed)
    for idx in range(count):
        a = gen(rng, idx)
        print(json.dumps({"i": idx, "a": a, "res": run_case(a)}))


# ---------------------------------------------------------------------------------------------
def compare(interp, count, seed, maxn=10):
    import subprocess
    import numpy as np
    la = np.linalg
    me = sys.argv[0]
    p = subprocess.run([interp, me, "dump", str(count), str(seed), str(maxn)], capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stderr[-2000:])
        print("dump failed")
        return 1
    lines = [ln for ln in p.stdout.split("\n") if ln.strip()]
    hard = {}
    soft = {}
    examples = {}
    total = {}

    def dec(x):
        if isinstance(x, dict):
            return np.array(x["re"]) + 1j * np.array(x["im"])
        if isinstance(x, list):
            return np.array([dec(t) for t in x]) if x and isinstance(x[0], (list, dict)) else np.array(x)
        return x

    def decv(x):
        # a tuple result: list of arrays
        return [dec(t) for t in x]

    def fail(kind, name, case, msg):
        d = hard if kind == "hard" else soft
        d[name] = d.get(name, 0) + 1
        examples.setdefault((kind, name), []).append((case, msg))

    def mset_close(w1, w2, tol):
        w1 = list(w1)
        w2 = list(w2)
        if len(w1) != len(w2):
            return False
        for z in w1:
            best = None
            for k, y in enumerate(w2):
                if abs(z - y) <= tol and (best is None or abs(z - y) < abs(z - w2[best])):
                    best = k
            if best is None:
                return False
            w2.pop(best)
        return True

    def cscale(A):
        return max(1e-300, float(np.abs(A).max())) if A.size else 1.0

    for ln in lines:
        rec = json.loads(ln)
        idx = rec["i"]
        a = rec["a"]
        res = rec["res"]
        A = np.array(a)
        m, n = A.shape
        sc = cscale(A)
        nrm2 = float(np.linalg.norm(A, 2)) if A.size else 1.0
        case = "case %d %dx%d kind %d" % (idx, m, n, idx % 16)
        for name in res:
            total[name] = total.get(name, 0) + 1

        def cmp_err(name, f):
            """numpy outcome: (value, None) or (None, error class)"""
            try:
                return f(), None
            except Exception as e:
                return None, type(e).__name__
        for name, f in (("det", lambda: la.det(A)), ("inv", lambda: la.inv(A)),
                        ("eigvals", lambda: la.eigvals(A)), ("eig", lambda: la.eig(A)),
                        ("svd", lambda: la.svd(A)), ("qr", lambda: la.qr(A)),
                        ("qr_c", lambda: la.qr(A, mode='complete')), ("pinv", lambda: la.pinv(A)),
                        ("rank", lambda: la.matrix_rank(A)), ("cond", lambda: la.cond(A))):
            if name not in res:
                continue
            r = res[name]
            ref, err = cmp_err(name, f)
            if isinstance(r, str):
                if err is None:
                    if name in ("inv", "cond") and np.linalg.cond(A) > 1e13:
                        continue
                    fail("hard", name, case, "we raised %s, numpy ok" % r)
                continue
            if err is not None:
                if name in ("inv", "cond", "det"):
                    fail("soft", name, case, "numpy raised %s, we returned" % err)
                else:
                    fail("hard", name, case, "numpy raised %s" % err)
                continue
            if name == "det":
                tol = 1e-9 * max(1.0, abs(ref), float(np.prod(np.abs(A).sum(axis=1)) if A.size else 1))
                if abs(r - ref) > tol:
                    fail("hard", name, case, "%r vs %r" % (r, ref))
            elif name == "inv":
                X = np.array(r)
                if np.linalg.norm(A @ X - np.eye(n)) > 1e-12 * max(1.0, np.linalg.norm(A) * np.linalg.norm(X)) * 1e3:
                    fail("hard", name, case, "residual")
            elif name == "eigvals":
                w = dec(r)
                tol = 1e-8 * max(1.0, sc) * (n ** 0.5) * 10
                if not mset_close(w, ref, tol * (1e4 if idx % 16 in (8, 10, 7) else 1)):
                    fail("hard", name, case, "values differ")
                elif len(w) == len(ref) and np.max(np.abs(np.array(w) - ref)) > tol and False:
                    pass
                else:
                    ordered = np.allclose(w, ref, rtol=0, atol=tol * (1e4 if idx % 16 in (8, 10) else 1))
                    if not ordered:
                        fail("soft", "eigvals-order", case, "order differs from LAPACK")
                    total["eigvals-order"] = total.get("eigvals-order", 0) + 1
            elif name == "eig":
                w = dec(r[0])
                V = dec(r[1])
                w2, V2 = ref
                deftol = 1e-8 if idx % 16 not in (8, 10, 7) else 1e-4
                if not mset_close(w, w2, deftol * max(1.0, sc) * n):
                    fail("hard", name, case, "values differ")
                    continue
                resid = np.abs(A @ V - V * w[None, :]).max() if A.size else 0.0
                bad = resid > 1e-8 * max(1.0, sc) * n * (1e3 if idx % 16 == 14 else 1)
                nrm = np.abs(np.linalg.norm(V, axis=0) - 1.0).max() if A.size else 0.0
                if bad or nrm > 1e-10:
                    fail("hard", name, case, "residual %g norm %g" % (resid, nrm))
                else:
                    total["eig-vec"] = total.get("eig-vec", 0) + 1
                    same = (np.allclose(w, w2, atol=1e-8 * max(1.0, sc)) and
                            np.allclose(V, V2, rtol=1e-6, atol=1e-7))
                    if not same:
                        fail("soft", "eig-vec", case, "vectors/ordering differ from LAPACK")
            elif name == "svd":
                U, s, Vt = [dec(t) for t in r]
                s2 = ref[1]
                if not np.allclose(s, s2, rtol=1e-8, atol=1e-10 * max(1.0, nrm2)):
                    fail("hard", name, case, "singular values")
                    continue
                k = min(m, n)
                Sm = np.zeros((m, n))
                Sm[:k, :k] = np.diag(s)
                if np.abs(U @ Sm @ Vt - A).max() > 1e-9 * max(1.0, sc) * 10 or \
                        np.abs(U.T @ U - np.eye(m)).max() > 1e-9 or np.abs(Vt @ Vt.T - np.eye(n)).max() > 1e-9:
                    fail("hard", name, case, "reconstruction/orthogonality")
                if sorted(s.tolist(), reverse=True) != s.tolist():
                    fail("hard", name, case, "not descending")
                if np.linalg.matrix_rank(A) == k and not (np.allclose(U, ref[0], atol=1e-6) and
                                                          np.allclose(Vt, ref[2], atol=1e-6)):
                    fail("soft", "svd-sign", case, "vectors (incl. the completed basis) differ from LAPACK")
            elif name in ("qr", "qr_c"):
                Q = dec(r[0])
                R = dec(r[1])
                Q2, R2 = ref
                if Q.shape != Q2.shape or R.shape != R2.shape:
                    fail("hard", name, case, "shape")
                    continue
                if np.abs(Q @ R - A).max() > 1e-9 * max(1.0, sc) * 10:
                    fail("hard", name, case, "reconstruction")
                elif np.linalg.matrix_rank(A) < min(m, n):
                    pass    # columns of Q after the rank are rounding noise: not reproducible
                elif not (np.allclose(R, R2, atol=1e-8 * max(1.0, sc)) and
                          np.allclose(Q[:, :min(m, n)], Q2[:, :min(m, n)], atol=1e-8)):
                    fail("soft", name, case, "Q/R differ from LAPACK (signs?)")
            elif name == "pinv":
                P = np.array(r)
                if not np.allclose(P, ref, rtol=1e-6, atol=1e-8 * max(1.0, 1.0 / max(sc, 1e-300))):
                    if np.linalg.cond(A) < 1e6:
                        fail("hard", name, case, "differs")
                    else:
                        fail("soft", name, case, "differs (rank deficient)")
            elif name == "rank":
                if int(r) != int(ref):
                    fail("hard", name, case, "%r vs %r" % (r, ref))
            elif name == "cond":
                if not np.isclose(r, ref, rtol=min(1.0, max(1e-6, 1e-14 * float(ref)))) and not (ref > 1e13 and r > 1e13):
                    fail("hard", name, case, "%r vs %r" % (r, ref))
        k = min(m, n)
        if "svd_r" in res and not isinstance(res["svd_r"], str):
            U, sv, Vt = [dec(t) for t in res["svd_r"]]
            ref_r = la.svd(A, full_matrices=False)
            if U.shape != (m, k) or Vt.shape != (k, n) or \
                    not np.allclose(sv, ref_r[1], rtol=1e-8, atol=1e-10 * max(1.0, nrm2)) or \
                    np.abs((U * sv) @ Vt - A).max() > 1e-9 * max(1.0, sc) * 10 or \
                    np.abs(U.T @ U - np.eye(k)).max() > 1e-9 or np.abs(Vt @ Vt.T - np.eye(k)).max() > 1e-9:
                fail("hard", "svd_r", case, "shape/values/reconstruction")
            elif np.linalg.matrix_rank(A) == k and not (np.allclose(U, ref_r[0], atol=1e-6) and
                                                        np.allclose(Vt, ref_r[2], atol=1e-6)):
                fail("soft", "svd_r-sign", case, "vectors differ from LAPACK")
        if "svdvals" in res and not isinstance(res["svdvals"], str):
            if not np.allclose(res["svdvals"], la.svd(A, compute_uv=False), rtol=1e-8,
                               atol=1e-10 * max(1.0, nrm2)):
                fail("hard", "svdvals", case, "values")
        if "eigh" in res and not isinstance(res["eigh"], str):
            S = A + A.T
            for key, up in (("eigh", 'L'), ("eighU", 'U')):
                w, V = [dec(t) for t in res[key]]
                w2, V2 = la.eigh(S, UPLO=up)
                if not np.allclose(w, w2, atol=1e-9 * max(1.0, sc) * n):
                    fail("hard", key, case, "values")
                    continue
                if np.abs(S @ V - V * w[None, :]).max() > 1e-9 * max(1.0, sc) * n or \
                        np.abs(V.T @ V - np.eye(n)).max() > 1e-9:
                    fail("hard", key, case, "residual/orthogonality")
                elif not np.allclose(V, V2, atol=1e-6) and np.allclose(np.diff(w2), 0, atol=1e-12) is False \
                        and np.min(np.diff(w2), initial=1.0) > 1e-6:
                    fail("soft", key + "-sign", case, "eigenvector signs differ from LAPACK")
            w = np.array(res["eigvalsh"])
            if not np.allclose(w, la.eigvalsh(S), atol=1e-9 * max(1.0, sc) * n):
                fail("hard", "eigvalsh", case, "values")
        if "solve" in res and not isinstance(res["solve"], str):
            b = np.array([[float((i * 7 + j * 3) % 5) - 2.0 for j in range(2)] for i in range(n)])
            X = np.array(res["solve"])
            if np.linalg.cond(A) < 1e10 and not np.allclose(X, la.solve(A, b), rtol=1e-6, atol=1e-8):
                fail("hard", "solve", case, "differs")
        elif "solve" in res:
            if np.linalg.cond(A) < 1e12:
                fail("hard", "solve", case, "we raised, cond %g" % np.linalg.cond(A))
        if "lstsq" in res and not isinstance(res["lstsq"], str):
            bb = np.array([float((i * 5) % 7) - 3.0 for i in range(m)])
            x, resd, rk = res["lstsq"]
            x2, resd2, rk2, s2 = la.lstsq(A, bb, rcond=None)
            if True:
                if not np.allclose(x, x2, rtol=1e-6, atol=1e-8 * max(1.0, 1.0 / max(sc, 1e-300))):
                    fail("hard" if np.linalg.cond(A) < 1e6 else "soft", "lstsq", case, "solution differs")
                if int(rk) != int(rk2):
                    fail("hard", "lstsq-rank", case, "%d vs %d" % (rk, rk2))
                if len(np.atleast_1d(resd)) != len(resd2) or (len(resd2) and not np.allclose(resd, resd2, rtol=1e-6, atol=1e-8)):
                    fail("hard", "lstsq-resid", case, "%r vs %r" % (resd, resd2))
    bad = sum(hard.values())
    print("cases %d" % len(lines))
    for name in sorted(set(list(total) + list(hard) + list(soft))):
        print("  %-14s checks %5d  hard %4d  soft %4d" % (name, total.get(name, 0), hard.get(name, 0), soft.get(name, 0)))
    for (kind, name), ex in sorted(examples.items()):
        for c, msg in ex[:3]:
            print("  [%s] %s: %s: %s" % (kind, name, c, msg))
    print("HARD FAILURES: %d" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "dump":
        dump(int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else None)
    elif len(sys.argv) >= 3 and sys.argv[1] == "compare":
        sys.exit(compare(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 2000,
                         int(sys.argv[4]) if len(sys.argv) > 4 else 1,
                         int(sys.argv[5]) if len(sys.argv) > 5 else 10))
    else:
        print(__doc__ if __doc__ else "usage: compare INTERP [N [SEED]] | dump N SEED")
        sys.exit(2)
