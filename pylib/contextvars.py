# contextvars: a single global context (no per-task isolation)
_MISSING = object()


class Token:
    MISSING = _MISSING

    def __init__(self, var, old_value):
        self.var = var
        self.old_value = old_value
        self._used = False

    def __repr__(self):
        return '<Token var=%r>' % (self.var,)


class ContextVar:
    def __init__(self, name, *, default=_MISSING):
        self._name = name
        self._default = default
        self._value = _MISSING

    @property
    def name(self):
        return self._name

    def get(self, *args):
        if self._value is not _MISSING:
            return self._value
        if args:
            return args[0]
        if self._default is not _MISSING:
            return self._default
        raise LookupError(self)

    def set(self, value):
        old = self._value
        self._value = value
        return Token(self, old)

    def reset(self, token):
        if token._used:
            raise RuntimeError('<Token> has already been used once')
        if token.var is not self:
            raise ValueError('<Token> was created by a different ContextVar')
        token._used = True
        self._value = token.old_value

    def __repr__(self):
        return '<ContextVar name=%r at 0x7f0000000000>' % (self._name,)


class Context:
    def __init__(self):
        self._saved = {}

    def run(self, callable, *args, **kwargs):
        return callable(*args, **kwargs)

    def copy(self):
        return Context()


def copy_context():
    return Context()
