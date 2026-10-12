"""abc: ABC / abstractmethod without enforcement (metaclasses are not supported)."""


class ABCMeta(type):
    pass


class ABC:
    @classmethod
    def register(cls, subclass):
        # virtual subclass registration is recorded but does not affect isinstance()
        reg = cls.__dict__.get('_abc_registry')
        if reg is None:
            reg = []
            cls._abc_registry = reg
        reg.append(subclass)
        return subclass


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
