# array: list-backed implementation with typed value checking and struct-based byte conversion
import struct as _struct

typecodes = 'bBuhHiIlLqQfd'
_SIZE = {'b': 1, 'B': 1, 'u': 4, 'h': 2, 'H': 2, 'i': 4, 'I': 4, 'l': 8, 'L': 8, 'q': 8, 'Q': 8, 'f': 4, 'd': 8}
_RANGE = {'b': (-128, 127), 'B': (0, 255), 'h': (-32768, 32767), 'H': (0, 65535), 'i': (-2 ** 31, 2 ** 31 - 1),
          'I': (0, 2 ** 32 - 1), 'l': (-2 ** 63, 2 ** 63 - 1), 'L': (0, 2 ** 64 - 1), 'q': (-2 ** 63, 2 ** 63 - 1),
          'Q': (0, 2 ** 64 - 1)}
_FMT = {'b': 'b', 'B': 'B', 'h': 'h', 'H': 'H', 'i': 'i', 'I': 'I', 'l': 'q', 'L': 'Q', 'q': 'q', 'Q': 'Q',
        'f': 'f', 'd': 'd'}


class array:
    def __init__(self, typecode, initializer=None):
        if not isinstance(typecode, str) or len(typecode) != 1:
            raise TypeError("array() argument 1 must be a unicode character, not %s" % type(typecode).__name__)
        if typecode not in typecodes:
            raise ValueError("bad typecode (must be b, B, u, w, h, H, i, I, l, L, q, Q, f or d)")
        self.typecode = typecode
        self.itemsize = _SIZE[typecode]
        self._a = []
        if initializer is not None:
            if isinstance(initializer, (bytes, bytearray)):
                self.frombytes(initializer)
            elif isinstance(initializer, str):
                if typecode != 'u':
                    raise TypeError("cannot use a str to initialize an array with typecode '%s'" % typecode)
                for ch in initializer:
                    self._a.append(ch)
            else:
                for x in initializer:
                    self.append(x)

    def _check(self, x):
        tc = self.typecode
        if tc == 'u':
            if not isinstance(x, str) or len(x) != 1:
                raise TypeError("array item must be a unicode character, not %s" % type(x).__name__)
            return x
        if tc in 'fd':
            if isinstance(x, bool) or isinstance(x, (int, float)):
                return float(x)
            raise TypeError("must be real number, not %s" % type(x).__name__)
        if not isinstance(x, int):
            raise TypeError("'%s' object cannot be interpreted as an integer" % type(x).__name__)
        lo, hi = _RANGE[tc]
        if x < lo or x > hi:
            if tc in 'bhilq':
                raise OverflowError("%s is %s than maximum" % ("signed integer", "greater" if x > hi else "less") if False else (
                    "signed %s is %s" % ({'b': 'char', 'h': 'short integer', 'i': 'integer', 'l': 'integer', 'q': 'integer'}[tc], "greater than maximum" if x > hi else "less than minimum")))
            if x < lo:
                raise OverflowError("unsigned %s is less than minimum" % ("byte integer" if tc == 'B' else "integer"))
            raise OverflowError("unsigned %s is greater than maximum" % ("byte integer" if tc == 'B' else "integer"))
        return int(x)

    def __len__(self):
        return len(self._a)

    def __iter__(self):
        return iter(list(self._a))

    def __getitem__(self, i):
        if isinstance(i, slice):
            r = array(self.typecode)
            r._a = self._a[i]
            return r
        try:
            return self._a[i]
        except IndexError:
            raise IndexError("array index out of range")

    def __setitem__(self, i, v):
        if isinstance(i, slice):
            if not isinstance(v, array) or v.typecode != self.typecode:
                raise TypeError("can only assign array (not \"%s\") to array slice" % type(v).__name__)
            self._a[i] = list(v._a)
            return
        try:
            self._a[i] = self._check(v)
        except IndexError:
            raise IndexError("array assignment index out of range")

    def __delitem__(self, i):
        del self._a[i]

    def __contains__(self, x):
        return x in self._a

    def __eq__(self, other):
        if not isinstance(other, array):
            return NotImplemented
        return self._a == other._a

    def __ne__(self, other):
        if not isinstance(other, array):
            return NotImplemented
        return self._a != other._a

    def __lt__(self, other):
        return self._a < other._a

    def __le__(self, other):
        return self._a <= other._a

    def __gt__(self, other):
        return self._a > other._a

    def __ge__(self, other):
        return self._a >= other._a

    def __add__(self, other):
        if not isinstance(other, array):
            raise TypeError("can only append array (not \"%s\") to array" % type(other).__name__)
        if other.typecode != self.typecode:
            raise TypeError("bad argument type for built-in operation")
        r = array(self.typecode)
        r._a = self._a + other._a
        return r

    def __iadd__(self, other):
        self.extend(other)
        return self

    def __mul__(self, n):
        r = array(self.typecode)
        r._a = self._a * n
        return r

    __rmul__ = __mul__

    def __imul__(self, n):
        self._a = self._a * n
        return self

    def __copy__(self):
        return array(self.typecode, self._a)

    def __deepcopy__(self, memo=None):
        return array(self.typecode, self._a)

    def __reduce__(self):
        return (array, (self.typecode, self.tolist()))

    def __repr__(self):
        if not self._a:
            return "array('%s')" % self.typecode
        if self.typecode == 'u':
            return "array('u', %r)" % ''.join(self._a)
        return "array('%s', %r)" % (self.typecode, self._a)

    __str__ = __repr__

    def append(self, x):
        self._a.append(self._check(x))

    def extend(self, it):
        if isinstance(it, array):
            if it.typecode != self.typecode:
                raise TypeError("can only extend with array of same kind")
            self._a.extend(it._a)
        else:
            for x in it:
                self.append(x)

    def insert(self, i, x):
        self._a.insert(i, self._check(x))

    def pop(self, i=-1):
        try:
            return self._a.pop(i)
        except IndexError:
            if not self._a:
                raise IndexError("pop from empty array")
            raise IndexError("pop index out of range")

    def remove(self, x):
        try:
            self._a.remove(x)
        except ValueError:
            raise ValueError("array.remove(x): x not in array")

    def index(self, x, start=0, stop=None):
        if stop is None:
            stop = len(self._a)
        n = len(self._a)
        if start < 0:
            start = max(0, start + n)
        if stop < 0:
            stop = max(0, stop + n)
        for k in range(start, min(stop, n)):
            if self._a[k] == x:
                return k
        raise ValueError("array.index(x): x not in array")

    def count(self, x):
        return self._a.count(x)

    def reverse(self):
        self._a.reverse()

    def tolist(self):
        return list(self._a)

    def byteswap(self):
        if self.typecode in 'bB':
            return
        data = self.tobytes()
        sz = self.itemsize
        out = b''
        for k in range(0, len(data), sz):
            out += data[k:k + sz][::-1]
        self._a = []
        self.frombytes(out)

    def buffer_info(self):
        return (0, len(self._a))

    def tobytes(self):
        tc = self.typecode
        if tc == 'u':
            return ''.join(self._a).encode('utf-32-le') if False else b''.join(_struct.pack('<I', ord(c)) for c in self._a)
        if not self._a:
            return b''
        return _struct.pack('<%d%s' % (len(self._a), _FMT[tc]), *self._a)

    def frombytes(self, data):
        sz = self.itemsize
        if len(data) % sz:
            raise ValueError("bytes length not a multiple of item size")
        n = len(data) // sz
        if n == 0:
            return
        if self.typecode == 'u':
            vals = _struct.unpack('<%dI' % n, bytes(data))
            for v in vals:
                self._a.append(chr(v))
            return
        vals = _struct.unpack('<%d%s' % (n, _FMT[self.typecode]), bytes(data))
        for v in vals:
            self._a.append(v)

    def tounicode(self):
        if self.typecode != 'u':
            raise ValueError("tounicode() may only be called on unicode type arrays ('u')")
        return ''.join(self._a)

    def fromunicode(self, s):
        if self.typecode != 'u':
            raise ValueError("fromunicode() may only be called on unicode type arrays ('u')")
        for ch in s:
            self._a.append(ch)

    def fromlist(self, lst):
        for x in lst:
            self._check(x)
        for x in lst:
            self._a.append(self._check(x))

    def tofile(self, f):
        f.write(self.tobytes())

    def fromfile(self, f, n):
        self.frombytes(f.read(n * self.itemsize))

    def clear(self):
        self._a = []


ArrayType = array
