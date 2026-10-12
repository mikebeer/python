# Introspection objects for the CREXX interpreter: the evaluator builds them through these constructors
# (function.__code__, sys._getframe, exception.__traceback__).


class code:
    def __init__(self, info):
        (self.co_name, self.co_filename, self.co_firstlineno, self.co_argcount, self.co_posonlyargcount,
         self.co_kwonlyargcount, self.co_varnames, self.co_flags) = info
        self.co_qualname = self.co_name
        self.co_nlocals = len(self.co_varnames)
        self.co_cellvars = ()
        self.co_freevars = ()
        self.co_names = ()
        self.co_consts = ()

    def __repr__(self):
        return '<code object %s, file "%s", line %d>' % (self.co_name, self.co_filename, self.co_firstlineno)


class frame:
    def __init__(self, info):
        self.f_code, self.f_lineno, self.f_back, self.f_globals, self.f_locals = info
        self.f_builtins = {}
        self.f_trace = None

    def __repr__(self):
        return "<frame at 0x0, file '%s', line %d, code %s>" % (self.f_code.co_filename, self.f_lineno, self.f_code.co_name)


class traceback:
    def __init__(self, info):
        self.tb_frame, self.tb_lineno, self.tb_next = info
        self.tb_lasti = 0

    def __repr__(self):
        return '<traceback object at 0x0>'


def mkcode(info):
    return code(info)


def mkframe(info):
    return frame(info)


def mktb(info):
    return traceback(info)


class GenericAlias:
    """list[int], dict[str, int]: the result of subscripting a built-in class"""

    def __init__(self, origin, args):
        self.__origin__ = origin
        if not isinstance(args, tuple):
            args = (args,)
        self.__args__ = args

    def __repr__(self):
        parts = []
        for a in self.__args__:
            if a is Ellipsis:
                parts.append('...')
            elif isinstance(a, type):
                parts.append(a.__name__)
            else:
                parts.append(repr(a))
        return self.__origin__.__name__ + '[' + ', '.join(parts) + ']'

    def __call__(self, *args, **kw):
        return self.__origin__(*args, **kw)

    def __eq__(self, other):
        return isinstance(other, GenericAlias) and self.__origin__ is other.__origin__ and self.__args__ == other.__args__

    def __hash__(self):
        return hash((id(self.__origin__), self.__args__))


def mkalias(t):
    return GenericAlias(t[0], t[1])
