"""enum: a minimal Enum / IntEnum / Flag / IntFlag / auto / unique.

Members are created by Enum.__init_subclass__ (called when a subclass is created).  Looking a member
up by value, iterating over the class and len(cls) need class-level special methods (a metaclass);
EnumMeta below provides them, and Enum.__new__ implements Color(value) for interpreters with __new__.
"""


class auto:
    _n = 0

    def __init__(self):
        self.value = None


class EnumMeta(type):
    def __iter__(cls):
        return iter(cls._member_list_)

    def __reversed__(cls):
        return iter(list(reversed(cls._member_list_)))

    def __len__(cls):
        return len(cls._member_list_)

    def __getitem__(cls, name):
        return cls._member_map_[name]

    def __contains__(cls, m):
        return isinstance(m, cls) and m in cls._member_list_

    def __call__(cls, value, *args):
        return cls._lookup(value)


EnumType = EnumMeta


def _is_member_candidate(name, value):
    if name[:1] == "_":
        return False
    if isinstance(value, (property, staticmethod, classmethod)):
        return False
    t = type(value).__name__
    if t in ("function", "builtin_function_or_method", "method", "type"):
        return False
    return True


class Enum(metaclass=EnumMeta):
    def __init_subclass__(cls, **kw):
        names = []
        values = {}
        items = list(vars(cls).items())
        cls._member_map_ = {}
        cls._value2member_map_ = {}
        cls._member_list_ = []
        cls._member_names_ = []
        cust_init = "__init__" in dict(items)
        last = []
        for name, value in items:
            if not _is_member_candidate(name, value):
                continue
            if isinstance(value, auto):
                value = cls._generate_next_value_(name, 1, len(last), last)
            elif name == "_ignore_":
                continue
            last.append(value)
            canon = None
            try:
                canon = cls._value2member_map_.get(value) if not isinstance(value, auto) else None
            except TypeError:
                canon = None
            if canon is not None:
                setattr(cls, name, canon)
                cls._member_map_[name] = canon
                continue
            m = object.__new__(cls)
            m._name_ = name
            m._value_ = value
            m.name = name
            m.value = value
            if cust_init:
                if isinstance(value, tuple):
                    m.__init__(*value)
                else:
                    m.__init__(value)
            setattr(cls, name, m)
            cls._member_map_[name] = m
            cls._member_list_.append(m)
            cls._member_names_.append(name)
            try:
                cls._value2member_map_[value] = m
            except TypeError:
                pass

    @staticmethod
    def _generate_next_value_(name, start, count, last_values):
        for v in reversed(last_values):
            if isinstance(v, int):
                return v + 1
        return start

    def __new__(cls, value):
        return cls._lookup(value)

    def __init__(self, *args):
        pass

    @classmethod
    def _lookup(cls, value):
        if isinstance(value, cls):
            return value
        try:
            return cls._value2member_map_[value]
        except (KeyError, TypeError):
            pass
        for m in cls._member_list_:
            if m._value_ == value:
                return m
        r = cls._missing_(value)
        if r is not None:
            return r
        raise ValueError("%r is not a valid %s" % (value, cls.__name__))

    @classmethod
    def _missing_(cls, value):
        return None

    @classmethod
    def _members(cls):
        return list(cls._member_list_)

    @property
    def __members__(self):
        return dict(self._member_map_)

    def __repr__(self):
        return "<%s.%s: %r>" % (type(self).__name__, self._name_, self._value_)

    def __str__(self):
        return "%s.%s" % (type(self).__name__, self._name_)

    def __format__(self, spec):
        return format(str(self), spec)

    def __hash__(self):
        return hash(self._name_)

    def __reduce__(self):
        return (type(self), (self._value_,))

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return self


class IntEnum(Enum):
    def __int__(self):
        return self._value_

    def __index__(self):
        return self._value_

    def __str__(self):
        return str(self._value_)

    def __format__(self, spec):
        return format(self._value_, spec)

    def __hash__(self):
        return hash(self._value_)

    def _v(self, o):
        return o._value_ if isinstance(o, Enum) else o

    def __eq__(self, o):
        return self._value_ == self._v(o)

    def __ne__(self, o):
        return self._value_ != self._v(o)

    def __lt__(self, o):
        return self._value_ < self._v(o)

    def __le__(self, o):
        return self._value_ <= self._v(o)

    def __gt__(self, o):
        return self._value_ > self._v(o)

    def __ge__(self, o):
        return self._value_ >= self._v(o)

    def __add__(self, o):
        return self._value_ + self._v(o)

    def __radd__(self, o):
        return self._v(o) + self._value_

    def __sub__(self, o):
        return self._value_ - self._v(o)

    def __rsub__(self, o):
        return self._v(o) - self._value_

    def __mul__(self, o):
        return self._value_ * self._v(o)

    def __rmul__(self, o):
        return self._v(o) * self._value_

    def __floordiv__(self, o):
        return self._value_ // self._v(o)

    def __truediv__(self, o):
        return self._value_ / self._v(o)

    def __mod__(self, o):
        return self._value_ % self._v(o)

    def __neg__(self):
        return -self._value_

    def __abs__(self):
        return abs(self._value_)

    def __bool__(self):
        return self._value_ != 0

    def __and__(self, o):
        return self._value_ & self._v(o)

    def __or__(self, o):
        return self._value_ | self._v(o)

    def __xor__(self, o):
        return self._value_ ^ self._v(o)

    def __lshift__(self, o):
        return self._value_ << self._v(o)

    def __rshift__(self, o):
        return self._value_ >> self._v(o)


class StrEnum(Enum):
    def __str__(self):
        return self._value_

    def __format__(self, spec):
        return format(self._value_, spec)

    def __hash__(self):
        return hash(self._value_)

    def __eq__(self, o):
        return self._value_ == (o._value_ if isinstance(o, Enum) else o)

    def __ne__(self, o):
        return not self.__eq__(o)

    def __add__(self, o):
        return self._value_ + o

    def __radd__(self, o):
        return o + self._value_

    @staticmethod
    def _generate_next_value_(name, start, count, last_values):
        return name.lower()


class Flag(Enum):
    @staticmethod
    def _generate_next_value_(name, start, count, last_values):
        if not last_values:
            return 1
        hi = 0
        for v in last_values:
            if v > hi:
                hi = v
        n = 1
        while n <= hi:
            n *= 2
        return n

    @classmethod
    def _make(cls, value):
        # a (possibly composite) member for an integer value
        try:
            return cls._value2member_map_[value]
        except KeyError:
            pass
        m = object.__new__(cls)
        names = []
        for mm in cls._member_list_:
            if mm._value_ != 0 and mm._value_ & value == mm._value_:
                names.append(mm._name_)
        m._name_ = "|".join(names) if names else None
        m._value_ = value
        m.name = m._name_
        m.value = value
        return m

    @classmethod
    def _lookup(cls, value):
        if isinstance(value, cls):
            return value
        if not isinstance(value, int):
            raise ValueError("%r is not a valid %s" % (value, cls.__name__))
        allbits = 0
        for mm in cls._member_list_:
            allbits |= mm._value_
        if value & ~allbits:
            raise ValueError("%r is not a valid %s" % (value, cls.__name__))
        return cls._make(value)

    def __or__(self, o):
        return type(self)._make(self._value_ | o._value_)

    def __and__(self, o):
        return type(self)._make(self._value_ & o._value_)

    def __xor__(self, o):
        return type(self)._make(self._value_ ^ o._value_)

    def __invert__(self):
        allbits = 0
        for mm in type(self)._member_list_:
            allbits |= mm._value_
        return type(self)._make(allbits & ~self._value_)

    def __contains__(self, o):
        return o._value_ & self._value_ == o._value_

    def __bool__(self):
        return self._value_ != 0

    def __eq__(self, o):
        return isinstance(o, Flag) and self._value_ == o._value_

    def __hash__(self):
        return hash(self._value_)

    def __repr__(self):
        return "<%s.%s: %r>" % (type(self).__name__, self._name_, self._value_)

    def __str__(self):
        return "%s.%s" % (type(self).__name__, self._name_)


class IntFlag(Flag):
    def __int__(self):
        return self._value_

    def __index__(self):
        return self._value_

    def __or__(self, o):
        return type(self)._make(self._value_ | (o._value_ if isinstance(o, Enum) else o))

    def __and__(self, o):
        return type(self)._make(self._value_ & (o._value_ if isinstance(o, Enum) else o))

    def __eq__(self, o):
        return self._value_ == (o._value_ if isinstance(o, Enum) else o)

    def __hash__(self):
        return hash(self._value_)


def unique(cls):
    for name in cls._member_map_:
        m = cls._member_map_[name]
        if m._name_ != name:
            raise ValueError("duplicate values found in %r: %s -> %s" % (cls, name, m._name_))
    return cls
