# weakref for interpreters without weak references: references are STRONG (objects are never collected
# through them); the API shape (ref, proxy, WeakValueDictionary, WeakKeyDictionary, WeakSet, WeakMethod,
# finalize) is the CPython one, so code written for weak references runs unchanged.
__all__ = ["ref", "proxy", "getweakrefcount", "getweakrefs", "WeakKeyDictionary", "ReferenceType", "ProxyType",
           "CallableProxyType", "ProxyTypes", "WeakValueDictionary", "WeakSet", "WeakMethod", "finalize"]


class ref:
    def __init__(self, obj, callback=None):
        self._obj = obj
        self._cb = callback

    def __call__(self):
        return self._obj

    def __eq__(self, other):
        if isinstance(other, ref):
            return self._obj == other._obj
        return NotImplemented

    def __ne__(self, other):
        if isinstance(other, ref):
            return self._obj != other._obj
        return NotImplemented

    def __hash__(self):
        return hash(self._obj)

    def __repr__(self):
        return "<weakref at 0x7f0000000000; to '%s' at 0x7f0000000000>" % type(self._obj).__name__

    @property
    def __callback__(self):
        return self._cb


ReferenceType = ref
ProxyType = ref
CallableProxyType = ref
ProxyTypes = (ref,)


class _Proxy:
    def __init__(self, obj, callback=None):
        object.__setattr__(self, '_obj', obj)

    def __getattr__(self, name):
        return getattr(object.__getattribute__(self, '_obj'), name)

    def __setattr__(self, name, value):
        setattr(object.__getattribute__(self, '_obj'), name, value)

    def __call__(self, *a, **k):
        return object.__getattribute__(self, '_obj')(*a, **k)

    def __repr__(self):
        return repr(object.__getattribute__(self, '_obj'))

    def __str__(self):
        return str(object.__getattribute__(self, '_obj'))

    def __len__(self):
        return len(object.__getattribute__(self, '_obj'))

    def __iter__(self):
        return iter(object.__getattribute__(self, '_obj'))

    def __getitem__(self, k):
        return object.__getattribute__(self, '_obj')[k]

    def __eq__(self, other):
        return object.__getattribute__(self, '_obj') == other

    def __bool__(self):
        return bool(object.__getattribute__(self, '_obj'))


def proxy(obj, callback=None):
    return _Proxy(obj, callback)


def getweakrefcount(obj):
    return 0


def getweakrefs(obj):
    return []


class WeakMethod(ref):
    def __init__(self, meth, callback=None):
        ref.__init__(self, meth, callback)

    def __eq__(self, other):
        if isinstance(other, WeakMethod):
            return self._obj == other._obj
        return NotImplemented

    def __hash__(self):
        return hash(self._obj)


class WeakValueDictionary:
    def __init__(self, other=(), /, **kw):
        self.data = {}
        self.update(other, **kw)

    def __getitem__(self, key):
        return self.data[key]

    def __setitem__(self, key, value):
        self.data[key] = value

    def __delitem__(self, key):
        del self.data[key]

    def __contains__(self, key):
        return key in self.data

    def __len__(self):
        return len(self.data)

    def __iter__(self):
        return iter(list(self.data))

    def __repr__(self):
        return "<%s at 0x7f0000000000>" % type(self).__name__

    def get(self, key, default=None):
        return self.data.get(key, default)

    def keys(self):
        return list(self.data.keys())

    def values(self):
        return list(self.data.values())

    def items(self):
        return list(self.data.items())

    def itervaluerefs(self):
        return [ref(v) for v in self.data.values()]

    def valuerefs(self):
        return [ref(v) for v in self.data.values()]

    def pop(self, key, *args):
        return self.data.pop(key, *args)

    def popitem(self):
        return self.data.popitem()

    def setdefault(self, key, default=None):
        return self.data.setdefault(key, default)

    def update(self, other=(), /, **kw):
        if hasattr(other, 'items'):
            other = other.items()
        for k, v in other:
            self.data[k] = v
        for k in kw:
            self.data[k] = kw[k]

    def clear(self):
        self.data.clear()

    def copy(self):
        return WeakValueDictionary(self.data)

    __copy__ = copy


class WeakKeyDictionary:
    def __init__(self, dict=None):
        self.data = {}
        if dict is not None:
            self.update(dict)

    def __getitem__(self, key):
        return self.data[key]

    def __setitem__(self, key, value):
        self.data[key] = value

    def __delitem__(self, key):
        del self.data[key]

    def __contains__(self, key):
        return key in self.data

    def __len__(self):
        return len(self.data)

    def __iter__(self):
        return iter(list(self.data))

    def __repr__(self):
        return "<%s at 0x7f0000000000>" % type(self).__name__

    def get(self, key, default=None):
        return self.data.get(key, default)

    def keys(self):
        return list(self.data.keys())

    def values(self):
        return list(self.data.values())

    def items(self):
        return list(self.data.items())

    def keyrefs(self):
        return [ref(k) for k in self.data]

    def pop(self, key, *args):
        return self.data.pop(key, *args)

    def popitem(self):
        return self.data.popitem()

    def setdefault(self, key, default=None):
        return self.data.setdefault(key, default)

    def update(self, dict=None, /, **kwargs):
        if dict is not None:
            if hasattr(dict, 'items'):
                dict = dict.items()
            for k, v in dict:
                self.data[k] = v
        for k in kwargs:
            self.data[k] = kwargs[k]

    def clear(self):
        self.data.clear()

    def copy(self):
        return WeakKeyDictionary(self.data)

    __copy__ = copy


class WeakSet:
    def __init__(self, data=None):
        self.data = []
        if data is not None:
            for x in data:
                self.add(x)

    def __iter__(self):
        return iter(list(self.data))

    def __len__(self):
        return len(self.data)

    def __contains__(self, item):
        return item in self.data

    def add(self, item):
        if item not in self.data:
            self.data.append(item)

    def discard(self, item):
        if item in self.data:
            self.data.remove(item)

    def remove(self, item):
        self.data.remove(item)

    def pop(self):
        return self.data.pop()

    def clear(self):
        self.data = []

    def update(self, other):
        for x in other:
            self.add(x)

    def copy(self):
        return WeakSet(self.data)

    def union(self, other):
        r = self.copy()
        r.update(other)
        return r

    def __or__(self, other):
        return self.union(other)

    def __repr__(self):
        return "<%s at 0x7f0000000000>" % type(self).__name__


class finalize:
    _registry = []

    def __init__(self, obj, func, /, *args, **kwargs):
        self._func = func
        self._args = args
        self._kwargs = kwargs
        self._alive = True
        self.atexit = True
        finalize._registry.append(self)

    def __call__(self, _=None):
        if self._alive:
            self._alive = False
            if self in finalize._registry:
                finalize._registry.remove(self)
            return self._func(*self._args, **self._kwargs)

    def detach(self):
        if self._alive:
            self._alive = False
            return (None, self._func, self._args, self._kwargs)

    def peek(self):
        if self._alive:
            return (None, self._func, self._args, self._kwargs)

    @property
    def alive(self):
        return self._alive
