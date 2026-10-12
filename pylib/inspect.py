# inspect: predicates and small helpers.  Functions carry no __code__ here, so signature() cannot work.
import types as _types

CO_VARARGS = 4
CO_VARKEYWORDS = 8


def isclass(object):
    return isinstance(object, type)


def ismodule(object):
    return isinstance(object, _types.ModuleType)


def isfunction(object):
    return type(object).__name__ == 'function' or type(object).__name__ == 'lambda'


def ismethod(object):
    return type(object).__name__ == 'method'


def isbuiltin(object):
    return type(object).__name__ == 'builtin_function_or_method'


def isroutine(object):
    return isbuiltin(object) or isfunction(object) or ismethod(object)


def isgeneratorfunction(obj):
    return False


def iscoroutinefunction(obj):
    return False


def isasyncgenfunction(obj):
    return False


def isgenerator(object):
    return type(object).__name__ == 'generator'


def iscoroutine(object):
    return False


def isawaitable(object):
    return False


def isabstract(object):
    return bool(getattr(object, '__abstractmethods__', False))


def istraceback(object):
    return type(object).__name__ == 'traceback'


def isframe(object):
    return type(object).__name__ == 'frame'


def iscode(object):
    return type(object).__name__ == 'code'


def isdatadescriptor(object):
    return hasattr(object, '__set__') or hasattr(object, '__delete__')


def ismethoddescriptor(object):
    return False


def getmembers(object, predicate=None):
    results = []
    for key in dir(object):
        try:
            value = getattr(object, key)
        except AttributeError:
            continue
        if not predicate or predicate(value):
            results.append((key, value))
    results.sort(key=lambda pair: pair[0])
    return results


def getmodule(object, _filename=None):
    return None


def getdoc(object):
    doc = getattr(object, '__doc__', None)
    if not isinstance(doc, str):
        return None
    return cleandoc(doc)


def cleandoc(doc):
    lines = doc.expandtabs().split('\n')
    margin = None
    for line in lines[1:]:
        content = len(line.lstrip())
        if content:
            indent = len(line) - content
            margin = indent if margin is None else min(margin, indent)
    if lines:
        lines[0] = lines[0].lstrip()
    if margin is not None:
        for i in range(1, len(lines)):
            lines[i] = lines[i][margin:]
    while lines and not lines[-1]:
        lines.pop()
    while lines and not lines[0]:
        lines.pop(0)
    return '\n'.join(lines)


def unwrap(func, *, stop=None):
    f = func
    seen = [id(f)]
    while hasattr(f, '__wrapped__'):
        if stop is not None and stop(f):
            break
        f = f.__wrapped__
        if id(f) in seen:
            raise ValueError('wrapper loop when unwrapping {!r}'.format(func))
        seen.append(id(f))
    return f


def getmro(cls):
    return cls.__mro__


def currentframe():
    return None


def stack(context=1):
    return []


def signature(obj, *, follow_wrapped=True):
    raise ValueError('no signature found for %r (functions have no __code__ in this interpreter)' % (obj,))
