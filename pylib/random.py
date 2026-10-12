# random: the exact CPython Mersenne Twister implementation lives in pyrandom.py (pure Python, bit-exact with
# CPython 3.13: seeding incl. sha512 str seeds, all distributions, getstate/setstate).
# While an interpreter keeps its own native `random` module, native names win and this file only supplies the
# names the native module lacks (Random class, triangular, betavariate, ...); the module level functions defined
# here then draw from the native generator so that random.seed() stays deterministic.  When the interpreter lets
# this file replace the native module (or the native module is renamed/removed) everything is exact.
import pyrandom as _p
from pyrandom import (Random, SystemRandom, NV_MAGICCONST, LOG4, SG_MAGICCONST, BPF, RECIP_BPF, TWOPI)

__all__ = _p.__all__ + ["SystemRandom"]

try:
    import random as _nat
    if not hasattr(_nat, 'seed') or type(_nat.seed).__name__ != 'builtin_function_or_method':
        _nat = None
except ImportError:
    _nat = None


class _NativeBacked(Random):
    """Used while the interpreter's own (non CPython-exact) native `random` module shadows this file:
    module level functions that the native module lacks draw from the native generator so that
    random.seed() keeps working.  When this file is the real `random` module, _nat is None and the
    exact Mersenne Twister above is used."""

    def seed(self, a=None, version=2):
        if a is None:
            return
        if not isinstance(a, int):
            Random.seed(self, a, version)
            a = self.getrandbits(32)
        _nat.seed(a)

    def random(self):
        return _nat.random()

    def getrandbits(self, k):
        r = 0
        n = 0
        while n < k:
            r = (r << 32) | int(_nat.random() * 4294967296.0)
            n += 32
        return r >> (n - k) if n > k else r

    def getstate(self):
        raise NotImplementedError("getstate needs the exact Mersenne Twister")

    def setstate(self, state):
        raise NotImplementedError("setstate needs the exact Mersenne Twister")


_inst = _NativeBacked() if _nat is not None else _p._inst
betavariate = _inst.betavariate
binomialvariate = _inst.binomialvariate
choice = _inst.choice
choices = _inst.choices
expovariate = _inst.expovariate
gammavariate = _inst.gammavariate
gauss = _inst.gauss
getrandbits = _inst.getrandbits
getstate = _inst.getstate
lognormvariate = _inst.lognormvariate
normalvariate = _inst.normalvariate
paretovariate = _inst.paretovariate
randbytes = _inst.randbytes
randint = _inst.randint
random = _inst.random
randrange = _inst.randrange
sample = _inst.sample
seed = _inst.seed
setstate = _inst.setstate
shuffle = _inst.shuffle
triangular = _inst.triangular
uniform = _inst.uniform
vonmisesvariate = _inst.vonmisesvariate
weibullvariate = _inst.weibullvariate
