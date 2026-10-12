# Pure Python copyreg (minimal)
__all__ = ["pickle", "constructor", "add_extension", "remove_extension", "clear_extension_cache"]

dispatch_table = {}


def pickle(ob_type, pickle_function, constructor_ob=None):
    if not callable(pickle_function):
        raise TypeError("reduction functions must be callable")
    dispatch_table[ob_type] = pickle_function
    if constructor_ob is not None:
        constructor(constructor_ob)


def constructor(object):
    if not callable(object):
        raise TypeError("constructors must be callable")


def _reconstructor(cls, base, state):
    if base is object:
        obj = object.__new__(cls)
    elif base is list:
        obj = base.__new__(cls)
        list.extend(obj, state)
    elif base is dict:
        obj = base.__new__(cls)
        dict.update(obj, state)
    elif base is set:
        obj = base.__new__(cls)
        set.update(obj, state)
    else:
        obj = base.__new__(cls, state)
    return obj


_HEAPTYPE = 1 << 9


def __newobj__(cls, *args):
    return object.__new__(cls) if not args else cls.__new__(cls, *args)


def __newobj_ex__(cls, args, kwargs):
    return cls.__new__(cls, *args, **kwargs)


def _slotnames(cls):
    names = []
    for c in getattr(cls, '__mro__', (cls,)):
        sl = c.__dict__.get('__slots__') if hasattr(c, '__dict__') else None
        if sl:
            if isinstance(sl, str):
                sl = (sl,)
            for name in sl:
                if name in ("__dict__", "__weakref__"):
                    continue
                names.append(name)
    return names


_extension_registry = {}
_inverted_registry = {}
_extension_cache = {}


def add_extension(module, name, code):
    code = int(code)
    if not 1 <= code <= 0x7fffffff:
        raise ValueError("code out of range")
    key = (module, name)
    if (_extension_registry.get(key) == code and _inverted_registry.get(code) == key):
        return
    if key in _extension_registry:
        raise ValueError("key %s is already registered with code %s" % (key, _extension_registry[key]))
    if code in _inverted_registry:
        raise ValueError("code %s is already in use for key %s" % (code, _inverted_registry[code]))
    _extension_registry[key] = code
    _inverted_registry[code] = key


def remove_extension(module, name, code):
    key = (module, name)
    if (_extension_registry.get(key) != code or _inverted_registry.get(code) != key):
        raise ValueError("key %s is not registered with code %s" % (key, code))
    del _extension_registry[key]
    del _inverted_registry[code]
    if code in _extension_cache:
        del _extension_cache[code]


def clear_extension_cache():
    _extension_cache.clear()
