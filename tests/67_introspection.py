# function attributes, frames, tracebacks, sys.exc_info, traceback / inspect / logging built on them
import sys, traceback, inspect, logging, types, io


def f(a, b=2, /, c=3, *args, d, e='x', **kw):
    return a


co = f.__code__
print(co.co_name, co.co_argcount, co.co_posonlyargcount, co.co_kwonlyargcount)
print(co.co_varnames[:6], co.co_filename.endswith('67_introspection.py'), co.co_firstlineno)
print(f.__defaults__, f.__kwdefaults__, f.__name__, f.__qualname__)
print(hasattr(f, '__code__'), type(co).__name__, bool(co.co_flags & 4), bool(co.co_flags & 8))


def g(x: int, y: str = 'q') -> bool:
    return True


print(g.__annotations__, g.__defaults__, g.__kwdefaults__)
print((lambda: 0).__code__.co_name, (lambda p, q: 0).__code__.co_argcount, len(f.__code__.co_varnames) > 5)


def inner_error():
    raise ValueError('bad')


def middle():
    inner_error()


try:
    middle()
except ValueError as e:
    tb = e.__traceback__
    names = []
    while tb is not None:
        names.append((tb.tb_frame.f_code.co_name, tb.tb_lineno))
        tb = tb.tb_next
    print(names)
    t, v, x = sys.exc_info()
    print(t.__name__, v is e, x is e.__traceback__)

print(sys.exc_info())


def who():
    fr = sys._getframe()
    return fr.f_code.co_name, fr.f_back.f_code.co_name, fr.f_lineno > 0, 'who' in fr.f_globals


def caller():
    return who()


print(caller())
v = 41


def lcl():
    z = 1
    return sys._getframe().f_locals['z'], sys._getframe(1).f_code.co_name


print(lcl())
print(sys._getframe().f_globals['v'], sys._getframe().f_code.co_name)

# traceback module
try:
    middle()
except ValueError as e:
    ex = traceback.extract_tb(e.__traceback__)
    print([(s.name, s.line) for s in ex])
    print(len(traceback.format_tb(e.__traceback__)))
    print(traceback.format_exception_only(e))
    text = traceback.format_exc()
    print(text.splitlines()[0], text.splitlines()[-1])
    print(sum(1 for l in text.splitlines() if l.startswith('  File ')))
    buf = io.StringIO()
    traceback.print_exc(file=buf)
    print(buf.getvalue() == text)

try:
    try:
        middle()
    except ValueError as e:
        raise RuntimeError('wrapped') from e
except RuntimeError:
    out = traceback.format_exc()
    print('direct cause' in out, out.splitlines()[-1])
st = traceback.extract_stack()
print(st[-1].name, st[-1].lineno > 0)


# inspect
def sig(a, b: int = 2, /, c=3, *args, d, e: str = 'x', **kw) -> int:
    pass


class K:
    def __init__(self, x, y=1):
        pass

    def m(self, z, *, w=0):
        pass


s = inspect.signature(sig)
print(s)
print(list(s.parameters), s.parameters['b'].default, s.parameters['b'].annotation is int)
print([p.kind.name for p in s.parameters.values()])
print(s.return_annotation is int, s.parameters['a'].default is inspect.Parameter.empty)
print(inspect.signature(K), inspect.signature(K(1).m), inspect.signature(lambda *a, **k: 0))
print(s.bind(1, 2, 3, d=4).arguments)
try:
    s.bind(1)
except TypeError as e:
    print('TypeError', e)
bound = inspect.signature(K.m).bind(object(), 5, w=6)
bound.apply_defaults()
print(sorted(k for k in bound.arguments))


def gen():
    yield 1


print(inspect.isgeneratorfunction(gen), inspect.isgeneratorfunction(f))
print(inspect.currentframe().f_code.co_name, inspect.stack()[0].function, inspect.isframe(inspect.currentframe()))
print(types.FunctionType is type(f), types.CodeType is type(co), types.GenericAlias is type(list[int]))

# logging with caller information
stream = io.StringIO()
h = logging.StreamHandler(stream)
h.setFormatter(logging.Formatter('%(funcName)s %(lineno)d %(filename)s %(levelname)s %(message)s'))
log = logging.getLogger('introspect')
log.addHandler(h)
log.setLevel(logging.DEBUG)
log.propagate = False


def do_log():
    log.info('hello')


do_log()
log.warning('top level')
try:
    1 / 0
except ZeroDivisionError:
    log.exception('failed')
lines = stream.getvalue().splitlines()
print(lines[:3], lines[3], lines[-1])
