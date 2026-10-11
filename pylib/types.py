"""types: a few type objects and SimpleNamespace."""


def _f():
    pass


class _C:
    def m(self):
        pass


FunctionType = type(_f)
LambdaType = FunctionType
BuiltinFunctionType = type(len)
BuiltinMethodType = BuiltinFunctionType
MethodType = type(_C().m)
NoneType = type(None)
def _g():
    yield


GeneratorType = type(_g())
ModuleType = type(__import__("sys"))


class SimpleNamespace:
    def __init__(self, **kw):
        for k in kw:
            setattr(self, k, kw[k])

    def __repr__(self):
        items = ["%s=%r" % (k, v) for k, v in vars(self).items()]
        return "namespace(%s)" % ", ".join(items)

    def __eq__(self, o):
        return isinstance(o, SimpleNamespace) and vars(self) == vars(o)
