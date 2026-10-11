"""collections: Counter, defaultdict, deque, OrderedDict, namedtuple, ChainMap, UserDict (plain classes)."""


class _MapBase:
    def __init__(self):
        self._d = {}

    def __len__(self):
        return len(self._d)

    def __iter__(self):
        return iter(self._d)

    def __reversed__(self):
        return reversed(list(self._d))

    def __contains__(self, k):
        return k in self._d

    def __getitem__(self, k):
        return self._d[k]

    def __setitem__(self, k, v):
        self._d[k] = v

    def __delitem__(self, k):
        del self._d[k]

    def __bool__(self):
        return len(self._d) > 0

    def keys(self):
        return self._d.keys()

    def values(self):
        return self._d.values()

    def items(self):
        return self._d.items()

    def get(self, k, default=None):
        return self._d.get(k, default)

    def pop(self, k, *default):
        return self._d.pop(k, *default)

    def popitem(self):
        return self._d.popitem()

    def setdefault(self, k, default=None):
        return self._d.setdefault(k, default)

    def clear(self):
        self._d.clear()

    def update(self, other=None, **kw):
        if other is not None:
            if isinstance(other, dict):
                for k in other:
                    self._d[k] = other[k]
            elif not isinstance(other, (list, tuple, set)) and hasattr(other, "keys"):
                for k in other.keys():
                    self._d[k] = other[k]
            else:
                for k, v in other:
                    self._d[k] = v
        for k in kw:
            self._d[k] = kw[k]

    def __eq__(self, other):
        if isinstance(other, dict):
            return self._d == other
        if isinstance(other, _MapBase):
            return self._d == other._d
        return False

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        raise TypeError("unhashable type: '%s'" % type(self).__name__)


class OrderedDict(_MapBase):
    def __init__(self, other=None, **kw):
        self._d = {}
        if other is not None or kw:
            self.update(other, **kw)

    def move_to_end(self, k, last=True):
        v = self._d.pop(k)
        if last:
            self._d[k] = v
        else:
            old = self._d
            self._d = {k: v}
            for kk in old:
                self._d[kk] = old[kk]

    def popitem(self, last=True):
        if not self._d:
            raise KeyError("dictionary is empty")
        k = list(self._d)[-1 if last else 0]
        return (k, self._d.pop(k))

    def copy(self):
        return OrderedDict(self._d)

    def __eq__(self, other):
        if isinstance(other, OrderedDict):
            return list(self._d.items()) == list(other._d.items())
        if isinstance(other, dict):
            return self._d == other
        return False

    def __repr__(self):
        if not self._d:
            return "OrderedDict()"
        return "OrderedDict(%r)" % (self._d,)

    @classmethod
    def fromkeys(cls, it, value=None):
        r = cls()
        for k in it:
            r._d[k] = value
        return r


class defaultdict(_MapBase):
    def __init__(self, default_factory=None, other=None, **kw):
        self._d = {}
        self.default_factory = default_factory
        if other is not None or kw:
            self.update(other, **kw)

    def __getitem__(self, k):
        d = self._d
        if k in d:
            return d[k]
        return self.__missing__(k)

    def __missing__(self, k):
        f = self.default_factory
        if f is None:
            raise KeyError(k)
        v = f()
        self._d[k] = v
        return v

    def copy(self):
        return defaultdict(self.default_factory, self._d)

    def __repr__(self):
        return "defaultdict(%r, %r)" % (self.default_factory, self._d)


class Counter(_MapBase):
    def __init__(self, iterable=None, **kw):
        self._d = {}
        if iterable is not None or kw:
            self.update(iterable, **kw)

    def __getitem__(self, k):
        d = self._d
        if k in d:
            return d[k]
        return 0

    def __missing__(self, k):
        return 0

    def update(self, iterable=None, **kw):
        d = self._d
        if iterable is not None:
            if isinstance(iterable, dict):
                for k in iterable:
                    d[k] = d.get(k, 0) + iterable[k]
            elif isinstance(iterable, _MapBase):
                for k in iterable._d:
                    d[k] = d.get(k, 0) + iterable._d[k]
            else:
                for e in iterable:
                    if e in d:
                        d[e] += 1
                    else:
                        d[e] = 1
        for k in kw:
            d[k] = d.get(k, 0) + kw[k]

    def subtract(self, iterable=None, **kw):
        d = self._d
        if iterable is not None:
            if isinstance(iterable, dict) or isinstance(iterable, _MapBase):
                for k in iterable.keys():
                    d[k] = d.get(k, 0) - iterable[k]
            else:
                for e in iterable:
                    d[e] = d.get(e, 0) - 1
        for k in kw:
            d[k] = d.get(k, 0) - kw[k]

    def total(self):
        return sum(self._d.values())

    def most_common(self, n=None):
        items = sorted(self._d.items(), key=lambda kv: kv[1], reverse=True)
        if n is None:
            return items
        return items[:n]

    def elements(self):
        for k in self._d:
            for _ in range(self._d[k]):
                yield k

    def copy(self):
        c = Counter()
        c._d = dict(self._d)
        return c

    def __delitem__(self, k):
        if k in self._d:
            del self._d[k]

    def __repr__(self):
        if not self._d:
            return "Counter()"
        return "Counter({%s})" % ", ".join(["%r: %r" % (k, v) for k, v in self.most_common()])

    def __eq__(self, other):
        if isinstance(other, Counter):
            for k in self._d:
                if self._d[k] != other._d.get(k, 0):
                    return False
            for k in other._d:
                if k not in self._d and other._d[k] != 0:
                    return False
            return True
        return _MapBase.__eq__(self, other)

    def __add__(self, other):
        r = Counter()
        for k in self._d:
            n = self._d[k] + other._d.get(k, 0)
            if n > 0:
                r._d[k] = n
        for k in other._d:
            if k not in self._d and other._d[k] > 0:
                r._d[k] = other._d[k]
        return r

    def __sub__(self, other):
        r = Counter()
        for k in self._d:
            n = self._d[k] - other._d.get(k, 0)
            if n > 0:
                r._d[k] = n
        for k in other._d:
            if k not in self._d and other._d[k] < 0:
                r._d[k] = -other._d[k]
        return r

    def __or__(self, other):
        r = Counter()
        for k in self._d:
            a = self._d[k]
            b = other._d.get(k, 0)
            n = a if a >= b else b
            if n > 0:
                r._d[k] = n
        for k in other._d:
            if k not in self._d and other._d[k] > 0:
                r._d[k] = other._d[k]
        return r

    def __and__(self, other):
        r = Counter()
        for k in self._d:
            if k in other._d:
                a = self._d[k]
                b = other._d[k]
                n = a if a <= b else b
                if n > 0:
                    r._d[k] = n
        return r

    def __pos__(self):
        r = Counter()
        for k in self._d:
            if self._d[k] > 0:
                r._d[k] = self._d[k]
        return r

    def __neg__(self):
        r = Counter()
        for k in self._d:
            if self._d[k] < 0:
                r._d[k] = -self._d[k]
        return r

    @classmethod
    def fromkeys(cls, iterable, v=None):
        raise NotImplementedError("Counter.fromkeys() is undefined.  Use Counter(iterable) instead.")


class deque:
    def __init__(self, iterable=None, maxlen=None):
        self._a = []
        self._h = 0
        self.maxlen = maxlen
        if iterable is not None:
            for x in iterable:
                self.append(x)

    def __len__(self):
        return len(self._a) - self._h

    def __bool__(self):
        return len(self._a) > self._h

    def __iter__(self):
        a = self._a
        for i in range(self._h, len(a)):
            yield a[i]

    def __reversed__(self):
        a = self._a
        for i in range(len(a) - 1, self._h - 1, -1):
            yield a[i]

    def append(self, x):
        a = self._a
        a.append(x)
        m = self.maxlen
        if m is not None and len(a) - self._h > m:
            self.popleft()

    def appendleft(self, x):
        m = self.maxlen
        if m is not None and len(self._a) - self._h >= m:
            if m == 0:
                return
            self.pop()
        h = self._h
        if h == 0:
            n = len(self._a) + 4
            self._a = [None] * n + self._a
            h = n
        h -= 1
        self._a[h] = x
        self._h = h

    def pop(self):
        if len(self._a) <= self._h:
            raise IndexError("pop from an empty deque")
        x = self._a.pop()
        if len(self._a) == self._h:
            self._a = []
            self._h = 0
        return x

    def popleft(self):
        a = self._a
        h = self._h
        if len(a) <= h:
            raise IndexError("pop from an empty deque")
        x = a[h]
        a[h] = None
        h += 1
        if h == len(a):
            self._a = []
            h = 0
        elif h > 32 and h * 2 > len(a):
            self._a = a[h:]
            h = 0
        self._h = h
        return x

    def extend(self, it):
        for x in list(it):
            self.append(x)

    def extendleft(self, it):
        for x in list(it):
            self.appendleft(x)

    def clear(self):
        self._a = []
        self._h = 0

    def copy(self):
        return deque(list(self), self.maxlen)

    def __copy__(self):
        return self.copy()

    def count(self, x):
        n = 0
        for y in self:
            if y == x:
                n += 1
        return n

    def index(self, x, start=0, stop=None):
        lst = list(self)
        if stop is None:
            stop = len(lst)
        for i in range(start, stop):
            if lst[i] == x:
                return i
        raise ValueError("%r is not in deque" % (x,))

    def insert(self, i, x):
        lst = list(self)
        if self.maxlen is not None and len(lst) >= self.maxlen:
            raise IndexError("deque already at its maximum size")
        lst.insert(i, x)
        self._a = lst
        self._h = 0

    def remove(self, x):
        lst = list(self)
        lst.remove(x)
        self._a = lst
        self._h = 0

    def reverse(self):
        lst = list(self)
        lst.reverse()
        self._a = lst
        self._h = 0

    def rotate(self, n=1):
        lst = list(self)
        k = len(lst)
        if k > 1:
            n = n % k
            if n:
                lst = lst[-n:] + lst[:-n]
        self._a = lst
        self._h = 0

    def _norm(self, i):
        n = len(self._a) - self._h
        if i < 0:
            i += n
        if i < 0 or i >= n:
            raise IndexError("deque index out of range")
        return self._h + i

    def __getitem__(self, i):
        if isinstance(i, slice):
            raise TypeError("sequence index must be integer, not 'slice'")
        return self._a[self._norm(i)]

    def __setitem__(self, i, v):
        self._a[self._norm(i)] = v

    def __delitem__(self, i):
        del self._a[self._norm(i)]

    def __contains__(self, x):
        for y in self:
            if y == x:
                return True
        return False

    def __eq__(self, other):
        if not isinstance(other, deque):
            return False
        return list(self) == list(other)

    def __ne__(self, other):
        return not self.__eq__(other)

    def __add__(self, other):
        r = self.copy()
        r.extend(other)
        return r

    def __iadd__(self, other):
        self.extend(other)
        return self

    def __mul__(self, n):
        return deque(list(self) * n, self.maxlen)

    def __hash__(self):
        raise TypeError("unhashable type: 'collections.deque'")

    def __repr__(self):
        if self.maxlen is None:
            return "deque(%r)" % (list(self),)
        return "deque(%r, maxlen=%d)" % (list(self), self.maxlen)


class _NamedTupleBase:
    _fields = ()
    _defaults = ()

    def __init__(self, *args, **kw):
        f = self._fields
        n = len(f)
        if len(args) > n:
            raise TypeError("%s() takes %d positional arguments but %d were given" % (type(self).__name__, n, len(args)))
        vals = list(args)
        if len(vals) < n:
            nd = len(self._defaults)
            for i in range(len(vals), n):
                name = f[i]
                if name in kw:
                    vals.append(kw.pop(name))
                elif i >= n - nd:
                    vals.append(self._defaults[i - (n - nd)])
                else:
                    raise TypeError("%s() missing 1 required positional argument: '%s'" % (type(self).__name__, name))
        if kw:
            for name in kw:
                raise TypeError("%s() got an unexpected keyword argument '%s'" % (type(self).__name__, name))
        for i in range(n):
            setattr(self, f[i], vals[i])
        self._t = tuple(vals)

    @classmethod
    def _make(cls, iterable):
        return cls(*tuple(iterable))

    def _asdict(self):
        return dict(zip(self._fields, self._t))

    def _replace(self, **kw):
        vals = list(self._t)
        for k in kw:
            if k not in self._fields:
                raise ValueError("Got unexpected field names: ['%s']" % k)
            vals[self._fields.index(k)] = kw[k]
        return type(self)(*vals)

    def __len__(self):
        return len(self._t)

    def __iter__(self):
        return iter(self._t)

    def __getitem__(self, i):
        return self._t[i]

    def __contains__(self, x):
        return x in self._t

    def __hash__(self):
        return hash(self._t)

    def _cmp_t(self, o):
        if isinstance(o, _NamedTupleBase):
            return o._t
        return o

    def __eq__(self, o):
        return self._t == self._cmp_t(o)

    def __ne__(self, o):
        return self._t != self._cmp_t(o)

    def __lt__(self, o):
        return self._t < self._cmp_t(o)

    def __le__(self, o):
        return self._t <= self._cmp_t(o)

    def __gt__(self, o):
        return self._t > self._cmp_t(o)

    def __ge__(self, o):
        return self._t >= self._cmp_t(o)

    def __add__(self, o):
        return self._t + tuple(o)

    def __mul__(self, n):
        return self._t * n

    def count(self, x):
        return self._t.count(x)

    def index(self, x):
        return self._t.index(x)

    def __getnewargs__(self):
        return self._t

    def __repr__(self):
        return "%s(%s)" % (type(self).__name__,
                           ", ".join([f + "=" + repr(v) for f, v in zip(self._fields, self._t)]))


def namedtuple(typename, field_names, rename=False, defaults=None, module=None):
    if isinstance(field_names, str):
        field_names = field_names.replace(",", " ").split()
    else:
        field_names = [str(x) for x in field_names]
    seen = set()
    for i in range(len(field_names)):
        n = field_names[i]
        if rename and (n in seen or n.startswith("_") or not n.isidentifier()):
            field_names[i] = n = "_%d" % i
        if n.startswith("_") and not rename:
            raise ValueError("Field names cannot start with an underscore: '%s'" % n)
        if n in seen:
            raise ValueError("Encountered duplicate field name: '%s'" % n)
        seen.add(n)
    d = {"_fields": tuple(field_names), "_defaults": tuple(defaults) if defaults is not None else ()}
    return type(typename, (_NamedTupleBase,), d)


class ChainMap(_MapBase):
    def __init__(self, *maps):
        self.maps = list(maps) if maps else [{}]

    def __getitem__(self, k):
        for m in self.maps:
            if k in m:
                return m[k]
        return self.__missing__(k)

    def __missing__(self, k):
        raise KeyError(k)

    def get(self, k, default=None):
        if k in self:
            return self[k]
        return default

    def __contains__(self, k):
        for m in self.maps:
            if k in m:
                return True
        return False

    def _merged(self):
        d = {}
        for m in reversed(self.maps):
            for k in m:
                d[k] = None
        return d

    def __iter__(self):
        return iter(self._merged())

    def __len__(self):
        return len(self._merged())

    def __bool__(self):
        for m in self.maps:
            if m:
                return True
        return False

    def keys(self):
        return list(self._merged())

    def values(self):
        return [self[k] for k in self._merged()]

    def items(self):
        return [(k, self[k]) for k in self._merged()]

    def __setitem__(self, k, v):
        self.maps[0][k] = v

    def __delitem__(self, k):
        if k in self.maps[0]:
            del self.maps[0][k]
        else:
            raise KeyError("Key not found in the first mapping: %r" % (k,))

    def pop(self, k, *default):
        if k in self.maps[0]:
            return self.maps[0].pop(k)
        if default:
            return default[0]
        raise KeyError("Key not found in the first mapping: %r" % (k,))

    def popitem(self):
        return self.maps[0].popitem()

    def clear(self):
        self.maps[0].clear()

    def update(self, other=None, **kw):
        m = self.maps[0]
        if other is not None:
            if not isinstance(other, (list, tuple, set)) and hasattr(other, "keys"):
                for k in other.keys():
                    m[k] = other[k]
            else:
                for k, v in other:
                    m[k] = v
        for k in kw:
            m[k] = kw[k]

    def setdefault(self, k, default=None):
        if k in self:
            return self[k]
        self.maps[0][k] = default
        return default

    def new_child(self, m=None):
        if m is None:
            m = {}
        return ChainMap(m, *self.maps)

    @property
    def parents(self):
        return ChainMap(*self.maps[1:])

    def copy(self):
        return ChainMap(self.maps[0].copy(), *self.maps[1:])

    def __eq__(self, other):
        if isinstance(other, (list, tuple, set, int, float, str)) or other is None:
            return False
        return dict(self.items()) == dict(other.items()) if hasattr(other, "items") else False

    def __repr__(self):
        return "ChainMap(%s)" % ", ".join([repr(m) for m in self.maps])


class UserDict(_MapBase):
    def __init__(self, other=None, **kw):
        self._d = {}
        self.data = self._d
        if other is not None or kw:
            self.update(other, **kw)

    def copy(self):
        return UserDict(self._d)

    def __repr__(self):
        return repr(self._d)


class UserList:
    def __init__(self, initlist=None):
        self.data = list(initlist) if initlist is not None else []

    def __len__(self):
        return len(self.data)

    def __iter__(self):
        return iter(self.data)

    def __getitem__(self, i):
        return self.data[i]

    def __setitem__(self, i, v):
        self.data[i] = v

    def __delitem__(self, i):
        del self.data[i]

    def __contains__(self, x):
        return x in self.data

    def __eq__(self, o):
        return self.data == (o.data if isinstance(o, UserList) else o)

    def append(self, x):
        self.data.append(x)

    def extend(self, it):
        self.data.extend(it)

    def insert(self, i, x):
        self.data.insert(i, x)

    def pop(self, i=-1):
        return self.data.pop(i)

    def remove(self, x):
        self.data.remove(x)

    def sort(self, key=None, reverse=False):
        self.data.sort(key=key, reverse=reverse)

    def reverse(self):
        self.data.reverse()

    def index(self, x):
        return self.data.index(x)

    def count(self, x):
        return self.data.count(x)

    def __add__(self, o):
        return type(self)(self.data + (o.data if isinstance(o, UserList) else list(o)))

    def __repr__(self):
        return repr(self.data)
