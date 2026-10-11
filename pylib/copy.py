"""copy: shallow and deep copy."""


class Error(Exception):
    pass


error = Error

def _f():
    pass


_ATOMIC = (int, float, bool, str, type(None), range, type, type(_f), type(len))
try:
    _ATOMIC = _ATOMIC + (bytes,)
except NameError:
    pass


def _copy_inst(x, deep, memo):
    cls = type(x)
    y = object.__new__(cls)
    if deep:
        memo[id(x)] = y
    for k, v in vars(x).items():
        if deep:
            v = deepcopy(v, memo)
        setattr(y, k, v)
    return y


def copy(x):
    t = type(x)
    if isinstance(x, _ATOMIC):
        return x
    if t is list:
        return x[:]
    if t is dict:
        return x.copy()
    if t is set:
        return x.copy()
    if t is tuple or t is frozenset:
        return x
    f = getattr(x, "__copy__", None)
    if f is not None:
        return f()
    if hasattr(x, "__dict__"):
        return _copy_inst(x, False, None)
    return x


def deepcopy(x, memo=None):
    if isinstance(x, _ATOMIC):
        return x
    if memo is None:
        memo = {}
    i = id(x)
    if i in memo:
        return memo[i]
    t = type(x)
    if t is list:
        y = []
        memo[i] = y
        for e in x:
            y.append(deepcopy(e, memo))
        return y
    if t is dict:
        y = {}
        memo[i] = y
        for k in x:
            y[deepcopy(k, memo)] = deepcopy(x[k], memo)
        return y
    if t is tuple:
        y = tuple([deepcopy(e, memo) for e in x])
        memo[i] = y
        return y
    if t is set:
        y = set()
        memo[i] = y
        for e in x:
            y.add(deepcopy(e, memo))
        return y
    if t is frozenset:
        return x
    f = getattr(x, "__deepcopy__", None)
    if f is not None:
        y = f(memo)
        memo[i] = y
        return y
    if hasattr(x, "__dict__"):
        return _copy_inst(x, True, memo)
    return x
