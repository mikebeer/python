"""functools in pure Python."""

_MISSING = object()

WRAPPER_ASSIGNMENTS = ("__module__", "__name__", "__qualname__", "__doc__")


def reduce(function, iterable, initial=_MISSING):
    it = iter(iterable)
    if initial is _MISSING:
        try:
            value = next(it)
        except StopIteration:
            raise TypeError("reduce() of empty iterable with no initial value")
    else:
        value = initial
    for x in it:
        value = function(value, x)
    return value


class partial:
    def __init__(self, func, *args, **keywords):
        if isinstance(func, partial):
            args = func.args + args
            k = dict(func.keywords)
            k.update(keywords)
            keywords = k
            func = func.func
        self.func = func
        self.args = args
        self.keywords = keywords

    def __call__(self, *args, **kw):
        if self.keywords:
            k = dict(self.keywords)
            k.update(kw)
            kw = k
        return self.func(*(self.args + args), **kw)

    def __repr__(self):
        parts = [repr(self.func)] + [repr(a) for a in self.args]
        parts = parts + ["%s=%r" % (k, self.keywords[k]) for k in self.keywords]
        return "functools.partial(%s)" % ", ".join(parts)


def update_wrapper(wrapper, wrapped, assigned=None, updated=None):
    for name in ("__module__", "__name__", "__qualname__", "__doc__"):
        try:
            setattr(wrapper, name, getattr(wrapped, name))
        except Exception:
            pass
    try:
        wrapper.__wrapped__ = wrapped
    except Exception:
        pass
    return wrapper


def wraps(wrapped, assigned=None, updated=None):
    def deco(wrapper):
        return update_wrapper(wrapper, wrapped)
    return deco


class _CacheInfo:
    def __init__(self, hits, misses, maxsize, currsize):
        self.hits = hits
        self.misses = misses
        self.maxsize = maxsize
        self.currsize = currsize

    def __repr__(self):
        return "CacheInfo(hits=%d, misses=%d, maxsize=%r, currsize=%d)" % (
            self.hits, self.misses, self.maxsize, self.currsize)

    def __iter__(self):
        return iter((self.hits, self.misses, self.maxsize, self.currsize))

    def __eq__(self, o):
        return tuple(self) == tuple(o)

    def __getitem__(self, i):
        return tuple(self)[i]


def _make_cache(func, maxsize, typed):
    cache = {}
    stats = [0, 0]   # hits, misses
    unbounded = maxsize is None

    def wrapper(*args, **kw):
        if kw:
            key = (args, tuple(sorted(kw.items())))
        else:
            key = args
        if key in cache:
            stats[0] += 1
            if unbounded:
                return cache[key]
            v = cache.pop(key)
            cache[key] = v
            return v
        stats[1] += 1
        v = func(*args, **kw)
        if maxsize == 0:
            return v
        cache[key] = v
        if not unbounded and len(cache) > maxsize:
            del cache[next(iter(cache))]
        return v

    def cache_info():
        return _CacheInfo(stats[0], stats[1], maxsize, len(cache))

    def cache_clear():
        cache.clear()
        stats[0] = 0
        stats[1] = 0

    wrapper.cache_info = cache_info
    wrapper.cache_clear = cache_clear
    wrapper.cache_parameters = lambda: {"maxsize": maxsize, "typed": typed}
    update_wrapper(wrapper, func)
    return wrapper


def lru_cache(maxsize=128, typed=False):
    if callable(maxsize):
        return _make_cache(maxsize, 128, typed)
    if maxsize is not None and maxsize < 0:
        maxsize = 0

    def deco(func):
        return _make_cache(func, maxsize, typed)
    return deco


def cache(func):
    return _make_cache(func, None, False)


def cmp_to_key(mycmp):
    class K:
        def __init__(self, obj):
            self.obj = obj

        def __lt__(self, other):
            return mycmp(self.obj, other.obj) < 0

        def __gt__(self, other):
            return mycmp(self.obj, other.obj) > 0

        def __eq__(self, other):
            return mycmp(self.obj, other.obj) == 0

        def __le__(self, other):
            return mycmp(self.obj, other.obj) <= 0

        def __ge__(self, other):
            return mycmp(self.obj, other.obj) >= 0

        def __hash__(self):
            raise TypeError("unhashable type")
    return K


def total_ordering(cls):
    lt = getattr(cls, "__lt__", None)
    le = getattr(cls, "__le__", None)
    gt = getattr(cls, "__gt__", None)
    ge = getattr(cls, "__ge__", None)
    if lt is None and le is None and gt is None and ge is None:
        raise ValueError("must define at least one ordering operation: < > <= >=")
    if lt is not None:
        if gt is None:
            cls.__gt__ = lambda self, o: not (lt(self, o) or self == o)
        if le is None:
            cls.__le__ = lambda self, o: lt(self, o) or self == o
        if ge is None:
            cls.__ge__ = lambda self, o: not lt(self, o)
    elif le is not None:
        cls.__lt__ = lambda self, o: le(self, o) and not self == o
        if gt is None:
            cls.__gt__ = lambda self, o: not le(self, o)
        if ge is None:
            cls.__ge__ = lambda self, o: not le(self, o) or self == o
    elif gt is not None:
        cls.__lt__ = lambda self, o: not (gt(self, o) or self == o)
        if le is None:
            cls.__le__ = lambda self, o: not gt(self, o)
        if ge is None:
            cls.__ge__ = lambda self, o: gt(self, o) or self == o
    else:
        cls.__lt__ = lambda self, o: not ge(self, o)
        cls.__le__ = lambda self, o: not ge(self, o) or self == o
        cls.__gt__ = lambda self, o: ge(self, o) and not self == o
    return cls


def singledispatch(func):
    registry = [(object, func)]

    def dispatch(cls):
        best = None
        bf = None
        for t, f in registry:
            if issubclass(cls, t):
                if best is None or issubclass(t, best):
                    best = t
                    bf = f
        return bf

    def register(cls, f=None):
        if f is None:
            def deco(g):
                registry.append((cls, g))
                return g
            return deco
        registry.append((cls, f))
        return f

    def wrapper(*args, **kw):
        if not args:
            raise TypeError("%s requires at least 1 positional argument" % func.__name__)
        return dispatch(type(args[0]))(*args, **kw)

    wrapper.register = register
    wrapper.dispatch = dispatch
    update_wrapper(wrapper, func)
    return wrapper
