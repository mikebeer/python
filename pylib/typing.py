"""typing: runtime stubs (annotations are ignored by the interpreters)."""

TYPE_CHECKING = False


class _Alias:
    def __init__(self, name, args=None):
        self._name = name
        self.__args__ = args if args is not None else ()

    def __getitem__(self, params):
        if not isinstance(params, tuple):
            params = (params,)
        return _Alias(self._name, params)

    def __call__(self, *a, **k):
        raise TypeError("Type %s cannot be instantiated" % self._name)

    def __repr__(self):
        if not self.__args__:
            return "typing." + self._name
        return "typing.%s[%s]" % (self._name, ", ".join([_r(a) for a in self.__args__]))

    def __eq__(self, o):
        return isinstance(o, _Alias) and o._name == self._name and o.__args__ == self.__args__

    def __hash__(self):
        return hash((self._name, self.__args__))

    def __or__(self, o):
        return _Alias("Union", (self, o))

    def __ror__(self, o):
        return _Alias("Union", (o, self))


def _r(a):
    if isinstance(a, type):
        return a.__name__ if a.__module__ == "builtins" else a.__module__ + "." + a.__name__
    if a is None:
        return "None"
    return repr(a)


Any = _Alias("Any")
Union = _Alias("Union")
Optional = _Alias("Optional")
List = _Alias("List")
Dict = _Alias("Dict")
Set = _Alias("Set")
FrozenSet = _Alias("FrozenSet")
Tuple = _Alias("Tuple")
Type = _Alias("Type")
Callable = _Alias("Callable")
Iterable = _Alias("Iterable")
Iterator = _Alias("Iterator")
Generator = _Alias("Generator")
Sequence = _Alias("Sequence")
MutableSequence = _Alias("MutableSequence")
Mapping = _Alias("Mapping")
MutableMapping = _Alias("MutableMapping")
MutableSet = _Alias("MutableSet")
AbstractSet = _Alias("AbstractSet")
Collection = _Alias("Collection")
Container = _Alias("Container")
Hashable = _Alias("Hashable")
Sized = _Alias("Sized")
Reversible = _Alias("Reversible")
Awaitable = _Alias("Awaitable")
Coroutine = _Alias("Coroutine")
AsyncIterator = _Alias("AsyncIterator")
AsyncIterable = _Alias("AsyncIterable")
AsyncGenerator = _Alias("AsyncGenerator")
Deque = _Alias("Deque")
DefaultDict = _Alias("DefaultDict")
OrderedDict = _Alias("OrderedDict")
Counter = _Alias("Counter")
ChainMap = _Alias("ChainMap")
Literal = _Alias("Literal")
Final = _Alias("Final")
ClassVar = _Alias("ClassVar")
Annotated = _Alias("Annotated")
TypeAlias = _Alias("TypeAlias")
NoReturn = _Alias("NoReturn")
Never = _Alias("Never")
Self = _Alias("Self")
LiteralString = _Alias("LiteralString")
Required = _Alias("Required")
NotRequired = _Alias("NotRequired")
Unpack = _Alias("Unpack")
Concatenate = _Alias("Concatenate")
ParamSpec = _Alias("ParamSpec")
TypeGuard = _Alias("TypeGuard")
IO = _Alias("IO")
TextIO = _Alias("TextIO")
BinaryIO = _Alias("BinaryIO")
Pattern = _Alias("Pattern")
Match = _Alias("Match")
SupportsInt = _Alias("SupportsInt")
SupportsFloat = _Alias("SupportsFloat")
SupportsAbs = _Alias("SupportsAbs")
SupportsIndex = _Alias("SupportsIndex")
SupportsRound = _Alias("SupportsRound")
ItemsView = _Alias("ItemsView")
KeysView = _Alias("KeysView")
ValuesView = _Alias("ValuesView")
ContextManager = _Alias("ContextManager")
AsyncContextManager = _Alias("AsyncContextManager")

Text = str
AnyStr = _Alias("AnyStr")


class TypeVar:
    def __init__(self, name, *constraints, bound=None, covariant=False, contravariant=False):
        self.__name__ = name
        self.__constraints__ = constraints
        self.__bound__ = bound

    def __repr__(self):
        return "~" + self.__name__


class Generic:
    def __class_getitem__(cls, params):
        return cls


class Protocol:
    def __class_getitem__(cls, params):
        return cls


def cast(typ, val):
    return val


def overload(f):
    return f


def final(f):
    return f


def no_type_check(f):
    return f


def runtime_checkable(c):
    return c


def NewType(name, tp):
    def new_type(x):
        return x
    new_type.__name__ = name
    new_type.__supertype__ = tp
    return new_type


def get_type_hints(obj, globalns=None, localns=None, include_extras=False):
    return dict(getattr(obj, "__annotations__", {}))


def get_origin(tp):
    return None


def get_args(tp):
    return getattr(tp, "__args__", ())


def is_typeddict(tp):
    return False


def assert_never(arg):
    raise AssertionError("Expected code to be unreachable")


def assert_type(val, typ):
    return val


def reveal_type(obj):
    return obj


class NamedTuple:
    def __new__(cls, *args, **kw):
        if cls is NamedTuple:
            import collections
            return collections.namedtuple(args[0], [f[0] for f in args[1]])
        return object.__new__(cls)

    def __init_subclass__(cls, **kw):
        import _clsinfo
        import collections
        names = [n for n, a in _clsinfo.annotations(cls)]
        defaults = []
        for n in names:
            if hasattr(cls, n):
                defaults.append(getattr(cls, n))
            elif defaults:
                raise TypeError("Non-default namedtuple field %s cannot follow default field" % n)
        cls._fields = tuple(names)
        cls._defaults = tuple(defaults)


class TypedDict:
    def __new__(cls, *args, **kw):
        return dict(*args, **kw)
