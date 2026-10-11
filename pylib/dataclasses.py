"""dataclasses: @dataclass (init/repr/eq/order/frozen/unsafe_hash), field, fields, asdict, astuple, replace.

Annotated field names come from cls.__annotations__ when the interpreter provides it; otherwise the
class statement is located in the module's source file and its `name: type [= default]` lines are read.
"""
import _clsinfo


class _MissingType:
    def __repr__(self):
        return "<dataclasses.MISSING>"


MISSING = _MissingType()


class KW_ONLY:
    pass


class FrozenInstanceError(AttributeError):
    pass


class Field:
    def __init__(self, default=MISSING, default_factory=MISSING, init=True, repr=True, hash=None,
                 compare=True, metadata=None, kw_only=MISSING):
        self.name = None
        self.type = None
        self.default = default
        self.default_factory = default_factory
        self.init = init
        self.repr = repr
        self.hash = hash
        self.compare = compare
        self.metadata = metadata if metadata is not None else {}
        self.kw_only = kw_only

    def __repr__(self):
        return "Field(name=%r,type=%r,default=%r,default_factory=%r,init=%r,repr=%r,hash=%r,compare=%r)" % (
            self.name, self.type, self.default, self.default_factory, self.init, self.repr, self.hash,
            self.compare)


def field(*, default=MISSING, default_factory=MISSING, init=True, repr=True, hash=None, compare=True,
          metadata=None, kw_only=MISSING):
    if default is not MISSING and default_factory is not MISSING:
        raise ValueError("cannot specify both default and default_factory")
    return Field(default, default_factory, init, repr, hash, compare, metadata, kw_only)


def _annotations(cls):
    return _clsinfo.annotations(cls)


def _is_classvar(ann):
    a = ann if isinstance(ann, str) else str(ann)
    return a.startswith("ClassVar") or a.startswith("typing.ClassVar") or "ClassVar[" in a


def _collect(cls, kw_only_all):
    fields = {}
    for b in reversed(cls.__bases__):
        bf = getattr(b, "__dataclass_fields__", None)
        if bf:
            for n in bf:
                fields[n] = bf[n]
    kw_only = kw_only_all
    for name, ann in _annotations(cls):
        a = ann if isinstance(ann, str) else str(ann)
        if _is_classvar(ann):
            continue
        if a == "KW_ONLY" or a == "dataclasses.KW_ONLY":
            kw_only = True
            continue
        d = getattr(cls, name, MISSING)
        if isinstance(d, Field):
            f = d
        else:
            f = Field(default=d)
            if isinstance(d, (list, dict, set)):
                raise ValueError("mutable default %r for field %s is not allowed: use default_factory" % (
                    type(d), name))
        f.name = name
        f.type = ann
        if f.kw_only is MISSING:
            f.kw_only = kw_only
        if name in fields:
            del fields[name]
        fields[name] = f
        if isinstance(d, Field):
            if f.default is MISSING:
                try:
                    delattr(cls, name)
                except Exception:
                    pass
            else:
                setattr(cls, name, f.default)
    return fields


def _plural(names):
    q = ["'" + n + "'" for n in names]
    if len(q) == 1:
        return q[0]
    if len(q) == 2:
        return q[0] + " and " + q[1]
    return ", ".join(q[:-1]) + ", and " + q[-1]


def _make_init(flds, frozen, cname=""):
    pos = [f for f in flds if f.init and not f.kw_only]
    kwo = [f for f in flds if f.init and f.kw_only]
    allf = flds

    def __init__(self, *args, **kw):
        n = len(args)
        if n > len(pos):
            nreq = len([x for x in pos if x.default is MISSING and x.default_factory is MISSING])
            if nreq == len(pos):
                raise TypeError("%s.__init__() takes %d positional arguments but %d were given" % (
                    cname, len(pos) + 1, n + 1))
            raise TypeError("%s.__init__() takes from %d to %d positional arguments but %d were given" % (
                cname, nreq + 1, len(pos) + 1, n + 1))
        missing = []
        vals = {}
        for i in range(len(pos)):
            f = pos[i]
            if i < n:
                if f.name in kw:
                    raise TypeError("%s.__init__() got multiple values for argument '%s'" % (cname, f.name))
                vals[f.name] = args[i]
            elif f.name in kw:
                vals[f.name] = kw.pop(f.name)
            elif f.default is not MISSING:
                vals[f.name] = f.default
            elif f.default_factory is not MISSING:
                vals[f.name] = f.default_factory()
            else:
                missing.append(f.name)
        for f in kwo:
            if f.name in kw:
                vals[f.name] = kw.pop(f.name)
            elif f.default is not MISSING:
                vals[f.name] = f.default
            elif f.default_factory is not MISSING:
                vals[f.name] = f.default_factory()
            else:
                missing.append(f.name)
        if kw:
            for k in kw:
                raise TypeError("%s.__init__() got an unexpected keyword argument '%s'" % (cname, k))
        if missing:
            kwm = [x.name for x in kwo if x.name in missing]
            raise TypeError("%s.__init__() missing %d required %s argument%s: %s" % (
                cname, len(missing), "keyword-only" if len(kwm) == len(missing) else "positional",
                "" if len(missing) == 1 else "s", _plural(missing)))
        for f in allf:
            if f.name in vals:
                v = vals[f.name]
            elif not f.init:
                if f.default is not MISSING:
                    v = f.default
                elif f.default_factory is not MISSING:
                    v = f.default_factory()
                else:
                    continue
            else:
                continue
            if frozen:
                object.__setattr__(self, f.name, v)
            else:
                setattr(self, f.name, v)
        pi = getattr(self, "__post_init__", None)
        if pi is not None:
            pi()
    return __init__


def _make_repr(flds):
    rf = [f for f in flds if f.repr]

    def __repr__(self):
        return "%s(%s)" % (type(self).__name__, ", ".join([f.name + "=" + repr(getattr(self, f.name)) for f in rf]))
    return __repr__


def _tuple(self, flds):
    return tuple([getattr(self, f.name) for f in flds])


def dataclass(cls=None, *, init=True, repr=True, eq=True, order=False, unsafe_hash=False, frozen=False,
              match_args=True, kw_only=False, slots=False, weakref_slot=False):
    def wrap(cls):
        fields = _collect(cls, kw_only)
        flds = list(fields.values())
        cls.__dataclass_fields__ = fields
        cls.__dataclass_params__ = (init, repr, eq, order, unsafe_hash, frozen)
        if init:
            cls.__init__ = _make_init(flds, frozen, cls.__name__)
        if repr:
            cls.__repr__ = _make_repr(flds)
        cf = [f for f in flds if f.compare]
        if eq:
            def __eq__(self, other):
                if other.__class__ is self.__class__:
                    return _tuple(self, cf) == _tuple(other, cf)
                return NotImplemented
            cls.__eq__ = __eq__
        if order:
            def __lt__(self, other):
                if other.__class__ is self.__class__:
                    return _tuple(self, cf) < _tuple(other, cf)
                return NotImplemented

            def __le__(self, other):
                if other.__class__ is self.__class__:
                    return _tuple(self, cf) <= _tuple(other, cf)
                return NotImplemented

            def __gt__(self, other):
                if other.__class__ is self.__class__:
                    return _tuple(self, cf) > _tuple(other, cf)
                return NotImplemented

            def __ge__(self, other):
                if other.__class__ is self.__class__:
                    return _tuple(self, cf) >= _tuple(other, cf)
                return NotImplemented
            cls.__lt__ = __lt__
            cls.__le__ = __le__
            cls.__gt__ = __gt__
            cls.__ge__ = __ge__
        if frozen:
            def __setattr__(self, name, value):
                raise FrozenInstanceError("cannot assign to field '%s'" % name)

            def __delattr__(self, name):
                raise FrozenInstanceError("cannot delete field '%s'" % name)
            cls.__setattr__ = __setattr__
            cls.__delattr__ = __delattr__
        hf = [f for f in flds if (f.compare if f.hash is None else f.hash)]
        if unsafe_hash or (eq and frozen):
            def __hash__(self):
                return hash(_tuple(self, hf))
            cls.__hash__ = __hash__
        elif eq:
            def __hash__(self):
                raise TypeError("unhashable type: '%s'" % type(self).__name__)
            cls.__hash__ = __hash__
        return cls
    if cls is None:
        return wrap
    return wrap(cls)


def _isbi(o):
    t = type(o)
    return t is int or t is float or t is str or t is list or t is tuple or t is dict or t is set or \
        t is bool or o is None or t is frozenset


def fields(class_or_instance):
    f = None if _isbi(class_or_instance) else getattr(class_or_instance, "__dataclass_fields__", None)
    if f is None:
        raise TypeError("must be called with a dataclass type or instance")
    return tuple(f.values())


def is_dataclass(obj):
    return not _isbi(obj) and getattr(obj, "__dataclass_fields__", None) is not None


def _is_dc_instance(obj):
    return is_dataclass(obj) and not isinstance(obj, type)


def asdict(obj, dict_factory=dict):
    if not _is_dc_instance(obj):
        raise TypeError("asdict() should be called on dataclass instances")
    return _asdict(obj, dict_factory)


def _asdict(obj, df):
    if _is_dc_instance(obj):
        return df([(f.name, _asdict(getattr(obj, f.name), df)) for f in fields(obj)])
    if isinstance(obj, (list, tuple)):
        return type(obj)([_asdict(v, df) for v in obj])
    if isinstance(obj, dict):
        return type(obj)([(_asdict(k, df), _asdict(v, df)) for k, v in obj.items()])
    import copy
    return copy.deepcopy(obj)


def astuple(obj, tuple_factory=tuple):
    if not _is_dc_instance(obj):
        raise TypeError("astuple() should be called on dataclass instances")
    return _astuple(obj, tuple_factory)


def _astuple(obj, tf):
    if _is_dc_instance(obj):
        return tf([_astuple(getattr(obj, f.name), tf) for f in fields(obj)])
    if isinstance(obj, (list, tuple)):
        return type(obj)([_astuple(v, tf) for v in obj])
    if isinstance(obj, dict):
        return type(obj)([(_astuple(k, tf), _astuple(v, tf)) for k, v in obj.items()])
    import copy
    return copy.deepcopy(obj)


def replace(obj, **changes):
    if not _is_dc_instance(obj):
        raise TypeError("replace() should be called on dataclass instances")
    for f in fields(obj):
        if f.init and f.name not in changes:
            changes[f.name] = getattr(obj, f.name)
    return type(obj)(**changes)


def make_dataclass(cls_name, fields, *, bases=(), namespace=None, **kw):
    ns = dict(namespace) if namespace else {}
    ann = []
    for item in fields:
        if isinstance(item, str):
            ann.append((item, "object", MISSING))
        elif len(item) == 2:
            ann.append((item[0], item[1], MISSING))
        else:
            ann.append((item[0], item[1], item[2]))
    c = type(cls_name, tuple(bases) if bases else (object,), ns)
    flds = {}
    for name, t, d in ann:
        f = d if isinstance(d, Field) else Field(default=d)
        f.name = name
        f.type = t
        f.kw_only = False
        flds[name] = f
    c.__dataclass_fields__ = flds
    return dataclass(c, **kw) if False else _finish(c, flds, kw)


def _finish(c, flds, kw):
    fl = list(flds.values())
    c.__init__ = _make_init(fl, kw.get("frozen", False), c.__name__)
    c.__repr__ = _make_repr(fl)
    cf = [f for f in fl if f.compare]

    def __eq__(self, other):
        if other.__class__ is self.__class__:
            return _tuple(self, cf) == _tuple(other, cf)
        return NotImplemented
    c.__eq__ = __eq__
    return c
