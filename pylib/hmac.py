# Pure Python hmac
import hashlib as _hashlib


def _tob(x):
    if isinstance(x, str):
        raise TypeError("Strings must be encoded before hashing")
    return bytes(x)


class HMAC:
    def __init__(self, key, msg=None, digestmod=''):
        if not digestmod:
            raise TypeError("Missing required parameter 'digestmod'.")
        self._dm = digestmod
        if isinstance(digestmod, str):
            self._ctor = lambda d=b'': _hashlib.new(digestmod, d)
        else:
            self._ctor = digestmod
        key = _tob(key)
        h = self._ctor()
        self.digest_size = h.digest_size
        self.block_size = h.block_size
        self.name = 'hmac-' + h.name
        if len(key) > self.block_size:
            key = self._ctor(key).digest()
        key = key + b'\x00' * (self.block_size - len(key))
        self._inner = self._ctor(bytes([b ^ 0x36 for b in key]))
        self._outer = self._ctor(bytes([b ^ 0x5c for b in key]))
        if msg is not None:
            self.update(msg)

    def update(self, msg):
        self._inner.update(_tob(msg))

    def copy(self):
        h = HMAC(b'', None, self._dm)
        h._inner = self._inner.copy()
        h._outer = self._outer.copy()
        return h

    def digest(self):
        o = self._outer.copy()
        o.update(self._inner.digest())
        return o.digest()

    def hexdigest(self):
        return self.digest().hex()


def new(key, msg=None, digestmod=''):
    return HMAC(key, msg, digestmod)


def digest(key, msg, digest):
    return HMAC(key, msg, digest).digest()


def compare_digest(a, b):
    if isinstance(a, str) and isinstance(b, str):
        a = a.encode()
        b = b.encode()
    if len(a) != len(b):
        return False
    r = 0
    for i in range(len(a)):
        r |= a[i] ^ b[i]
    return r == 0
