"""abc: ABC / abstractmethod without enforcement (metaclasses are not supported)."""


class ABCMeta(type):
    pass


class ABC:
    pass


def abstractmethod(f):
    try:
        f.__isabstractmethod__ = True
    except Exception:
        pass
    return f


abstractclassmethod = classmethod
abstractstaticmethod = staticmethod
abstractproperty = property


def get_cache_token():
    return 0
