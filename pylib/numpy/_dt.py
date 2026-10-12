# dtype objects for the CREXX numpy subset: a str (so str(a.dtype), a.dtype == 'float64', dtype=a.dtype keep
# working) that also compares equal to the Python types float / int / complex / bool.
_ALIAS = {
    'float64': 'float64', 'f8': 'float64', 'float': 'float64', 'double': 'float64', 'd': 'float64',
    'float_': 'float64',
    'int64': 'int64', 'i8': 'int64', 'int': 'int64', 'long': 'int64', 'int_': 'int64',
    'complex128': 'complex128', 'c16': 'complex128', 'complex': 'complex128', 'D': 'complex128',
    'complex_': 'complex128',
    'bool': 'bool', '?': 'bool', 'bool_': 'bool',
}
_CACHE = {}


def _canon(o):
    if o is float:
        return 'float64'
    if o is int:
        return 'int64'
    if o is complex:
        return 'complex128'
    if o is bool:
        return 'bool'
    if isinstance(o, str):
        return _ALIAS.get(o.lower() if len(o) > 1 else o, o)
    try:
        return o.name
    except Exception:
        return None


class dtype(str):
    def __eq__(self, o):
        n = self.name
        return n == _canon(o)

    def __ne__(self, o):
        n = self.name
        return n != _canon(o)

    def __hash__(self):
        return hash(self.name)

    def __repr__(self):
        return "dtype('" + self.name + "')"

    @property
    def kind(self):
        n = self.name
        if n == 'float64':
            return 'f'
        if n == 'int64':
            return 'i'
        if n == 'complex128':
            return 'c'
        return 'b'

    @property
    def itemsize(self):
        n = self.name
        if n == 'complex128':
            return 16
        if n == 'bool':
            return 1
        return 8

    @property
    def type(self):
        n = self.name
        if n == 'float64':
            return float
        if n == 'int64':
            return int
        if n == 'complex128':
            return complex
        return bool


def _dt(name):
    d = _CACHE.get(name)
    if d is None:
        d = dtype(name)
        d.name = name
        _CACHE[name] = d
    return d
