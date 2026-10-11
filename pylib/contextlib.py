"""contextlib: contextmanager, suppress, closing, nullcontext, ExitStack, redirect_stdout."""
import sys as _sys


class AbstractContextManager:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return None


class _GeneratorContextManager:
    def __init__(self, func, args, kwds):
        self.gen = func(*args, **kwds)
        self.func = func
        self.args = args
        self.kwds = kwds

    def __enter__(self):
        try:
            return next(self.gen)
        except StopIteration:
            raise RuntimeError("generator didn't yield")

    def __exit__(self, typ, value, tb):
        if typ is None:
            try:
                next(self.gen)
            except StopIteration:
                return False
            raise RuntimeError("generator didn't stop")
        if value is None:
            value = typ()
        try:
            self.gen.throw(value)
        except StopIteration as exc:
            return exc is not value
        except BaseException as exc:
            if exc is value:
                return False
            raise
        raise RuntimeError("generator didn't stop after throw()")

    def __call__(self, f):
        def inner(*a, **k):
            with _GeneratorContextManager(self.func, self.args, self.kwds):
                return f(*a, **k)
        return inner


def contextmanager(func):
    def helper(*args, **kwds):
        return _GeneratorContextManager(func, args, kwds)
    helper.__name__ = func.__name__
    helper.__doc__ = func.__doc__
    return helper


class suppress:
    def __init__(self, *exceptions):
        self._exceptions = exceptions

    def __enter__(self):
        pass

    def __exit__(self, exctype, excinst, exctb):
        return exctype is not None and issubclass(exctype, self._exceptions)


class closing:
    def __init__(self, thing):
        self.thing = thing

    def __enter__(self):
        return self.thing

    def __exit__(self, *exc_info):
        self.thing.close()


class nullcontext:
    def __init__(self, enter_result=None):
        self.enter_result = enter_result

    def __enter__(self):
        return self.enter_result

    def __exit__(self, *excinfo):
        pass


class redirect_stdout:
    _stream = "stdout"

    def __init__(self, new_target):
        self._new_target = new_target
        self._old = []

    def __enter__(self):
        self._old.append(getattr(_sys, self._stream))
        setattr(_sys, self._stream, self._new_target)
        return self._new_target

    def __exit__(self, exctype, excinst, exctb):
        setattr(_sys, self._stream, self._old.pop())


class redirect_stderr(redirect_stdout):
    _stream = "stderr"


class ExitStack:
    def __init__(self):
        self._cbs = []

    def __enter__(self):
        return self

    def enter_context(self, cm):
        r = cm.__enter__()
        self._cbs.append(cm.__exit__)
        return r

    def push(self, exit):
        self._cbs.append(exit)
        return exit

    def callback(self, callback, *args, **kwds):
        def _exit(a, b, c):
            callback(*args, **kwds)
            return False
        self._cbs.append(_exit)
        return callback

    def pop_all(self):
        n = ExitStack()
        n._cbs = self._cbs
        self._cbs = []
        return n

    def close(self):
        self.__exit__(None, None, None)

    def __exit__(self, typ, val, tb):
        suppressed = False
        while self._cbs:
            cb = self._cbs.pop()
            try:
                if cb(typ, val, tb):
                    suppressed = True
                    typ = None
                    val = None
                    tb = None
            except BaseException as e:
                typ = type(e)
                val = e
                tb = None
                suppressed = False
        if typ is not None and not suppressed:
            raise val
        return suppressed
