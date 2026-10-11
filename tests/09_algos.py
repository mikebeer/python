def sieve(n):
    flags = [True] * (n + 1)
    flags[0] = flags[1] = False
    for i in range(2, int(n ** 0.5) + 1):
        if flags[i]:
            for j in range(i * i, n + 1, i):
                flags[j] = False
    return [i for i, f in enumerate(flags) if f]
print(sieve(60))
def qsort(a):
    if len(a) < 2: return a
    p = a[0]
    return qsort([x for x in a[1:] if x < p]) + [p] + qsort([x for x in a[1:] if x >= p])
print(qsort([5, 3, 8, 1, 9, 2, 7, 3]))
def bsearch(a, t):
    lo, hi = 0, len(a) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        if a[mid] == t: return mid
        if a[mid] < t: lo = mid + 1
        else: hi = mid - 1
    return -1
print(bsearch([1, 3, 5, 7, 9, 11], 7), bsearch([1, 3], 4))
def gcd(a, b):
    while b: a, b = b, a % b
    return a
print(gcd(48, 18), gcd(2 ** 64, 2 ** 40 * 3))
def perms(s):
    if len(s) <= 1: return [s]
    r = []
    for i, c in enumerate(s):
        for p in perms(s[:i] + s[i + 1:]):
            r.append(c + p)
    return r
print(perms("abc"))
memo = {}
def collatz(n):
    if n == 1: return 0
    if n in memo: return memo[n]
    r = 1 + collatz(n // 2 if n % 2 == 0 else 3 * n + 1)
    memo[n] = r
    return r
print(max(range(1, 200), key=collatz), collatz(27))
mat = [[1, 2], [3, 4]]
def mmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(2)) for j in range(2)] for i in range(2)]
r = mat
for _ in range(5): r = mmul(r, mat)
print(r)
print(sum(int(c) for c in str(2 ** 1000)))
print("".join(reversed("stressed")), "racecar" == "racecar"[::-1])
w = "the quick brown fox".split()
print(sorted(w, key=lambda s: (len(s), s)), max(w, key=len), " ".join(x.capitalize() for x in w))
print(bin(37)[2:], int("100101", 2), sum(1 for c in bin(255) if c == "1"))
