import numpy as np

seed = 12345


def rnd():
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed / 2147483648.0


def rmat(n, kind):
    rows = []
    for i in range(n):
        row = []
        for j in range(n):
            x = rnd() * 4.0 - 2.0
            if kind == 1:
                x = float(round(x * 2))
            elif kind == 2 and i < j:
                x = 0.0
            elif kind == 3 and i > j:
                x = 0.0
            elif kind == 4 and rnd() < 0.6:
                x = 0.0
            row.append(x)
        rows.append(row)
    return rows


def companion(roots):
    # monic polynomial with the given (real) roots, then its companion matrix
    c = [1.0]
    for r in roots:
        d = c + [0.0]
        for k in range(len(c)):
            d[k + 1] -= r * c[k]
        c = d
    n = len(c) - 1
    m = [[0.0] * n for _ in range(n)]
    for j in range(n):
        m[0][j] = -c[j + 1]
    for i in range(1, n):
        m[i][i - 1] = 1.0
    return m


def fix(x):
    r = round(x, 5) + 0.0
    return r


def show(label, w):
    out = []
    for z in w.tolist():
        z = complex(z)
        out.append((fix(z.real), fix(z.imag)))
    print(label, out)


def residual(m, w, v):
    n = len(m)
    wl = w.tolist()
    vl = v.tolist()
    worst = 0.0
    for k in range(n):
        lam = complex(wl[k])
        nrm = 0.0
        for i in range(n):
            nrm += abs(complex(vl[i][k])) ** 2
        s = 0.0
        for i in range(n):
            t = -lam * complex(vl[i][k])
            for j in range(n):
                t += m[i][j] * complex(vl[j][k])
            s += abs(t) ** 2
        worst = max(worst, s ** 0.5, abs(nrm - 1.0))
    return worst < 1e-8


cases = []
cases.append(("cubic", [[0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [-6.0, -11.0, -6.0]]))
cases.append(("diag", [[3.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 2.0]]))
cases.append(("lower", [[2.0, 0.0, 0.0], [1.0, 3.0, 0.0], [4.0, 5.0, 6.0]]))
cases.append(("sym2", [[2.0, 1.0], [1.0, 2.0]]))
cases.append(("rot", [[0.0, -1.0], [1.0, 0.0]]))
cases.append(("gen2", [[1.0, 2.0], [3.0, 4.0]]))
cases.append(("rep", [[2.0, 0.0, 0.0], [0.0, 2.0, 0.0], [0.0, 0.0, 5.0]]))
cases.append(("perm", [[0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0, 0.0, 0.0, 0.0]]))
cases.append(("comp5", companion([1.0, 2.0, 3.0, 4.0, 5.0])))
cases.append(("comp6", companion([-3.0, -2.0, -1.0, 0.5, 1.5, 7.0])))
for n in range(2, 9):
    for kind in range(5):
        cases.append(("r%d_%d" % (n, kind), rmat(n, kind)))
    cases.append(("r%d_s" % n, [[0.0] * n for _ in range(n)]))
    m = cases[-1][1]
    for i in range(n):
        for j in range(i + 1):
            m[i][j] = m[j][i] = float(round((rnd() * 2.0 - 1.0) * 6)) / 2.0

for name, m in cases:
    a = np.array(m)
    w = np.linalg.eigvals(a)
    show(name, w)
    w2, v = np.linalg.eig(a)
    same = True
    for x, y in zip(w.tolist(), w2.tolist()):
        if abs(complex(x) - complex(y)) > 1e-8:
            same = False
    print(name, "eig==eigvals", same, "residual ok", residual(m, w2, v))

print(np.round(np.linalg.eig(np.array([[0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [-6.0, -11.0, -6.0]]))[1].real, 5).tolist())
print(np.round(np.linalg.eig(np.array([[2.0, 0.0, 0.0], [1.0, 3.0, 0.0], [4.0, 5.0, 6.0]]))[1].real, 5).tolist())
print(np.round(np.linalg.eig(np.diag([3.0, 1.0, 2.0]))[1].real, 5).tolist())
print(np.round(np.sort(np.roots([1.0, -6.0, 11.0, -6.0]).real), 6).tolist())
