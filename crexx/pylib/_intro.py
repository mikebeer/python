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
