"""itertools in pure Python (generators)."""


def chain(*iterables):
    for it in iterables:
        for x in it:
            yield x


def _from_iterable(iterables):
    for it in iterables:
        for x in it:
            yield x


chain.from_iterable = _from_iterable


def islice(iterable, *args):
    n = len(args)
    if n == 1:
        start = 0
        stop = args[0]
        step = 1
    else:
        start = args[0]
        stop = args[1]
        step = 1
        if n > 2:
            step = args[2]
        if start is None:
            start = 0
        if step is None:
            step = 1
    if start < 0 or (stop is not None and stop < 0) or step <= 0:
        raise ValueError("Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.")
    it = iter(iterable)
    i = 0
    nxt = start
    if stop is not None and nxt >= stop:
        return
    for x in it:
        if i == nxt:
            yield x
            nxt += step
            if stop is not None and nxt >= stop:
                return
        i += 1


def count(start=0, step=1):
    n = start
    while True:
        yield n
        n += step


def cycle(iterable):
    saved = []
    for x in iterable:
        yield x
        saved.append(x)
    if not saved:
        return
    while True:
        for x in saved:
            yield x


def repeat(obj, times=None):
    if times is None:
        while True:
            yield obj
    else:
        for _ in range(times):
            yield obj


def accumulate(iterable, func=None, initial=None):
    it = iter(iterable)
    total = initial
    if initial is None:
        try:
            total = next(it)
        except StopIteration:
            return
    yield total
    if func is None:
        for x in it:
            total = total + x
            yield total
    else:
        for x in it:
            total = func(total, x)
            yield total


def groupby(iterable, key=None):
    it = iter(iterable)
    try:
        v = next(it)
    except StopIteration:
        return
    k = v if key is None else key(v)
    while True:
        grp = [v]
        done = True
        for v in it:
            k2 = v if key is None else key(v)
            if k2 == k:
                grp.append(v)
            else:
                done = False
                break
        yield (k, iter(grp))
        if done:
            return
        k = k2


def zip_longest(*iterables, fillvalue=None):
    its = [iter(x) for x in iterables]
    n = len(its)
    if n == 0:
        return
    alive = n
    while True:
        row = []
        for i in range(n):
            it = its[i]
            if it is None:
                row.append(fillvalue)
            else:
                try:
                    row.append(next(it))
                except StopIteration:
                    alive -= 1
                    its[i] = None
                    row.append(fillvalue)
        if alive == 0:
            return
        yield tuple(row)


def starmap(function, iterable):
    for args in iterable:
        yield function(*args)


def takewhile(predicate, iterable):
    for x in iterable:
        if predicate(x):
            yield x
        else:
            return


def dropwhile(predicate, iterable):
    it = iter(iterable)
    for x in it:
        if not predicate(x):
            yield x
            break
    for x in it:
        yield x


def filterfalse(predicate, iterable):
    for x in iterable:
        if predicate is None:
            if not x:
                yield x
        elif not predicate(x):
            yield x


def compress(data, selectors):
    for d, s in zip(data, selectors):
        if s:
            yield d


def pairwise(iterable):
    it = iter(iterable)
    try:
        a = next(it)
    except StopIteration:
        return
    for b in it:
        yield (a, b)
        a = b


def batched(iterable, n, strict=False):
    if n < 1:
        raise ValueError("n must be at least one")
    it = iter(iterable)
    while True:
        b = []
        for x in it:
            b.append(x)
            if len(b) == n:
                break
        if not b:
            return
        if strict and len(b) < n:
            raise ValueError("batched(): incomplete batch")
        yield tuple(b)
        if len(b) < n:
            return


class _TeeData:
    def __init__(self, it):
        self.it = it
        self.buf = []
        self.base = 0


def _tee_iter(data, pos):
    while True:
        i = pos - data.base
        if i >= len(data.buf):
            try:
                x = next(data.it)
            except StopIteration:
                return
            data.buf.append(x)
        else:
            x = data.buf[i]
        pos += 1
        yield x


def tee(iterable, n=2):
    data = _TeeData(iter(iterable))
    return tuple([_tee_iter(data, 0) for _ in range(n)])


def product(*iterables, repeat=1):
    pools = [tuple(p) for p in iterables] * repeat
    n = len(pools)
    for p in pools:
        if not p:
            return
    idx = [0] * n
    yield tuple([p[0] for p in pools])
    if n == 0:
        return
    while True:
        i = n - 1
        while i >= 0:
            idx[i] += 1
            if idx[i] < len(pools[i]):
                break
            idx[i] = 0
            i -= 1
        if i < 0:
            return
        yield tuple([pools[j][idx[j]] for j in range(n)])


def permutations(iterable, r=None):
    pool = tuple(iterable)
    n = len(pool)
    if r is None:
        r = n
    if r > n or r < 0:
        return
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    yield tuple([pool[i] for i in indices[:r]])
    while n:
        i = r - 1
        while i >= 0:
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
                i -= 1
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple([pool[k] for k in indices[:r]])
                break
        if i < 0:
            return


def combinations(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if r > n or r < 0:
        return
    indices = list(range(r))
    yield tuple([pool[i] for i in indices])
    while True:
        i = r - 1
        while i >= 0 and indices[i] == i + n - r:
            i -= 1
        if i < 0:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple([pool[k] for k in indices])


def combinations_with_replacement(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if not n and r:
        return
    indices = [0] * r
    yield tuple([pool[i] for i in indices])
    while True:
        i = r - 1
        while i >= 0 and indices[i] == n - 1:
            i -= 1
        if i < 0:
            return
        v = indices[i] + 1
        for j in range(i, r):
            indices[j] = v
        yield tuple([pool[k] for k in indices])
