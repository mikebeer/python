# inspect: predicates and small helpers.  signature() needs function __code__ / __defaults__ / __kwdefaults__
# / __annotations__ and currentframe()/stack() need sys._getframe (the C interpreter has both).
import sys as _sys
import types as _types

CO_VARARGS = 4
CO_VARKEYWORDS = 8
CO_GENERATOR = 32


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
    f = getattr(obj, '__func__', obj)
    co = getattr(f, '__code__', None)
    return co is not None and bool(co.co_flags & CO_GENERATOR)


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
    f = getattr(_sys, '_getframe', None)
    return f(1) if f is not None else None


class FrameInfo(tuple):
    def __new__(cls, frame, filename, lineno, function, code_context, index):
        return tuple.__new__(cls, (frame, filename, lineno, function, code_context, index))

    frame = property(lambda self: self[0])
    filename = property(lambda self: self[1])
    lineno = property(lambda self: self[2])
    function = property(lambda self: self[3])
    code_context = property(lambda self: self[4])
    index = property(lambda self: self[5])


def getframeinfo(frame, context=1):
    if istraceback(frame):
        lineno = frame.tb_lineno
        frame = frame.tb_frame
    else:
        lineno = frame.f_lineno
    co = frame.f_code
    lines = None
    try:
        with open(co.co_filename) as fh:
            lines = fh.read().split('\n')
    except Exception:
        pass
    if lines and context > 0:
        start = max(0, min(lineno - 1 - context // 2, len(lines) - context))
        lines = [l + '\n' for l in lines[start:start + context]]
        index = lineno - 1 - start
    else:
        lines = None
        index = None
    return FrameInfo(frame, co.co_filename, lineno, co.co_name, lines, index)


def stack(context=1):
    f = getattr(_sys, '_getframe', None)
    out = []
    fr = f(1) if f is not None else None
    while fr is not None:
        out.append(getframeinfo(fr, context))
        fr = fr.f_back
    return out


def getouterframes(frame, context=1):
    out = []
    while frame is not None:
        out.append(getframeinfo(frame, context))
        frame = frame.f_back
    return out


def trace(context=1):
    tb = _sys.exc_info()[2]
    out = []
    while tb is not None:
        out.append(getframeinfo(tb, context))
        tb = tb.tb_next
    return out


def getsourcefile(object):
    co = getattr(getattr(object, '__func__', object), '__code__', None)
    return co.co_filename if co is not None else None


def getfile(object):
    f = getsourcefile(object)
    if f is None:
        raise TypeError('module, class, method, function, traceback, frame, or code object was expected, got %s'
                        % type(object).__name__)
    return f


class _empty:
    """Marker object for Signature.empty and Parameter.empty."""


class _ParameterKind(int):
    _names = {}

    def __new__(cls, value, name):
        self = int.__new__(cls, value)
        self._name = name
        return self

    def __str__(self):
        return self._name

    def __repr__(self):
        return '<_ParameterKind.%s: %d>' % (self._name, int(self))

    @property
    def name(self):
        return self._name

    @property
    def description(self):
        return {0: 'positional-only', 1: 'positional or keyword', 2: 'variadic positional',
                3: 'keyword-only', 4: 'variadic keyword'}[int(self)]


_POSITIONAL_ONLY = _ParameterKind(0, 'POSITIONAL_ONLY')
_POSITIONAL_OR_KEYWORD = _ParameterKind(1, 'POSITIONAL_OR_KEYWORD')
_VAR_POSITIONAL = _ParameterKind(2, 'VAR_POSITIONAL')
_KEYWORD_ONLY = _ParameterKind(3, 'KEYWORD_ONLY')
_VAR_KEYWORD = _ParameterKind(4, 'VAR_KEYWORD')


def formatannotation(annotation, base_module=None):
    if getattr(annotation, '__module__', None) == 'typing':
        return repr(annotation).replace('typing.', '')
    if isinstance(annotation, type):
        if annotation.__module__ in ('builtins', base_module):
            return annotation.__qualname__
        return annotation.__module__ + '.' + annotation.__qualname__
    return repr(annotation)


class Parameter:
    POSITIONAL_ONLY = _POSITIONAL_ONLY
    POSITIONAL_OR_KEYWORD = _POSITIONAL_OR_KEYWORD
    VAR_POSITIONAL = _VAR_POSITIONAL
    KEYWORD_ONLY = _KEYWORD_ONLY
    VAR_KEYWORD = _VAR_KEYWORD
    empty = _empty

    def __init__(self, name, kind, *, default=_empty, annotation=_empty):
        if not isinstance(kind, _ParameterKind):
            raise ValueError('value %r is not a valid Parameter.kind' % (kind,))
        if default is not _empty and kind in (_VAR_POSITIONAL, _VAR_KEYWORD):
            raise ValueError('%s parameters cannot have default values' % kind.description)
        if not isinstance(name, str):
            raise TypeError('name must be a str, not a %s' % type(name).__name__)
        if not name.isidentifier():
            raise ValueError('%r is not a valid parameter name' % name)
        self._name = name
        self._kind = kind
        self._default = default
        self._annotation = annotation

    name = property(lambda self: self._name)
    kind = property(lambda self: self._kind)
    default = property(lambda self: self._default)
    annotation = property(lambda self: self._annotation)

    def replace(self, *, name=_empty, kind=_empty, default=_empty, annotation=_empty):
        if name is _empty:
            name = self._name
        if kind is _empty:
            kind = self._kind
        if default is _empty:
            default = self._default
        if annotation is _empty:
            annotation = self._annotation
        return type(self)(name, kind, default=default, annotation=annotation)

    def __str__(self):
        kind = self.kind
        formatted = self._name
        if self._annotation is not _empty:
            formatted = '{}: {}'.format(formatted, formatannotation(self._annotation))
        if self._default is not _empty:
            if self._annotation is not _empty:
                formatted = '{} = {}'.format(formatted, repr(self._default))
            else:
                formatted = '{}={}'.format(formatted, repr(self._default))
        if kind == _VAR_POSITIONAL:
            formatted = '*' + formatted
        elif kind == _VAR_KEYWORD:
            formatted = '**' + formatted
        return formatted

    def __repr__(self):
        return '<{} "{}">'.format(type(self).__name__, self)

    def __hash__(self):
        return hash((self._name, int(self._kind), self._annotation, self._default))

    def __eq__(self, other):
        if self is other:
            return True
        if not isinstance(other, Parameter):
            return NotImplemented
        return (self._name == other._name and self._kind == other._kind and
                self._default == other._default and self._annotation == other._annotation)


class BoundArguments:
    def __init__(self, signature, arguments):
        self.arguments = arguments
        self._signature = signature

    signature = property(lambda self: self._signature)

    @property
    def args(self):
        args = []
        for name, param in self._signature.parameters.items():
            if param.kind in (_VAR_KEYWORD, _KEYWORD_ONLY):
                break
            if name not in self.arguments:
                break
            arg = self.arguments[name]
            if param.kind == _VAR_POSITIONAL:
                args.extend(arg)
            else:
                args.append(arg)
        return tuple(args)

    @property
    def kwargs(self):
        kwargs = {}
        kwargs_started = False
        for name, param in self._signature.parameters.items():
            if not kwargs_started:
                if param.kind in (_VAR_KEYWORD, _KEYWORD_ONLY):
                    kwargs_started = True
                elif name not in self.arguments:
                    kwargs_started = True
                    continue
            if not kwargs_started:
                continue
            if name not in self.arguments:
                continue
            arg = self.arguments[name]
            if param.kind == _VAR_KEYWORD:
                kwargs.update(arg)
            else:
                kwargs[name] = arg
        return kwargs

    def apply_defaults(self):
        new_arguments = {}
        for name, param in self._signature.parameters.items():
            if name in self.arguments:
                new_arguments[name] = self.arguments[name]
            elif param.default is not _empty:
                new_arguments[name] = param.default
            elif param.kind == _VAR_POSITIONAL:
                new_arguments[name] = ()
            elif param.kind == _VAR_KEYWORD:
                new_arguments[name] = {}
        self.arguments = new_arguments

    def __eq__(self, other):
        if self is other:
            return True
        if not isinstance(other, BoundArguments):
            return NotImplemented
        return self._signature == other._signature and self.arguments == other.arguments

    def __repr__(self):
        args = []
        for arg, value in self.arguments.items():
            args.append('{}={!r}'.format(arg, value))
        return '<{} ({})>'.format(type(self).__name__, ', '.join(args))


class Signature:
    empty = _empty

    def __init__(self, parameters=None, *, return_annotation=_empty, __validate_parameters__=True):
        if parameters is None:
            params = {}
        else:
            params = {}
            seen_default = False
            top = -1
            for param in parameters:
                kind = param.kind
                name = param.name
                if kind < top:
                    raise ValueError('wrong parameter order: %s before %s' % (
                        Parameter.__dict__.get('_x', None) or _kindname(top), _kindname(kind)))
                top = int(kind)
                if kind in (_POSITIONAL_ONLY, _POSITIONAL_OR_KEYWORD):
                    if param.default is _empty:
                        if seen_default:
                            raise ValueError('non-default argument follows default argument')
                    else:
                        seen_default = True
                if name in params:
                    raise ValueError('duplicate parameter name: %r' % name)
                params[name] = param
        self._parameters = params
        self._return_annotation = return_annotation

    parameters = property(lambda self: dict(self._parameters))
    return_annotation = property(lambda self: self._return_annotation)

    def replace(self, *, parameters=_empty, return_annotation=_empty):
        if parameters is _empty:
            parameters = list(self._parameters.values())
        if return_annotation is _empty:
            return_annotation = self._return_annotation
        return type(self)(parameters, return_annotation=return_annotation)

    def __eq__(self, other):
        if self is other:
            return True
        if not isinstance(other, Signature):
            return NotImplemented
        return (list(self._parameters.values()) == list(other._parameters.values()) and
                self._return_annotation == other._return_annotation)

    def __hash__(self):
        return hash((tuple(self._parameters.values()), self._return_annotation))

    def _bind(self, args, kwargs, partial=False):
        arguments = {}
        parameters = iter(self._parameters.values())
        arg_vals = iter(args)
        parameters_ex = ()
        kwargs_param = None
        while True:
            try:
                arg_val = next(arg_vals)
            except StopIteration:
                try:
                    param = next(parameters)
                except StopIteration:
                    break
                else:
                    if param.kind == _VAR_POSITIONAL:
                        break
                    elif param.name in kwargs:
                        if param.kind == _POSITIONAL_ONLY:
                            raise TypeError('%r parameter is positional only, but was passed as a keyword'
                                            % param.name)
                        parameters_ex = (param,)
                        break
                    elif (param.kind == _VAR_KEYWORD or param.default is not _empty):
                        parameters_ex = (param,)
                        break
                    else:
                        if partial:
                            parameters_ex = (param,)
                            break
                        raise TypeError('missing a required argument: %r' % param.name)
            else:
                try:
                    param = next(parameters)
                except StopIteration:
                    raise TypeError('too many positional arguments') from None
                else:
                    if param.kind in (_VAR_KEYWORD, _KEYWORD_ONLY):
                        raise TypeError('too many positional arguments') from None
                    if param.kind == _VAR_POSITIONAL:
                        values = [arg_val]
                        values.extend(arg_vals)
                        arguments[param.name] = tuple(values)
                        break
                    if param.name in kwargs and param.kind != _POSITIONAL_ONLY:
                        raise TypeError('multiple values for argument %r' % param.name)
                    arguments[param.name] = arg_val
        kwargs_param = None
        for param in list(parameters_ex) + list(parameters):
            if param.kind == _VAR_KEYWORD:
                kwargs_param = param
                continue
            if param.kind == _VAR_POSITIONAL:
                continue
            param_name = param.name
            try:
                arg_val = kwargs.pop(param_name)
            except KeyError:
                if (not partial and param.kind != _VAR_POSITIONAL and param.default is _empty):
                    raise TypeError('missing a required argument: %r' % param_name) from None
            else:
                if param.kind == _POSITIONAL_ONLY:
                    raise TypeError('%r parameter is positional only, but was passed as a keyword' % param.name)
                arguments[param_name] = arg_val
        if kwargs:
            if kwargs_param is not None:
                arguments[kwargs_param.name] = kwargs
            else:
                raise TypeError('got an unexpected keyword argument %r' % next(iter(kwargs)))
        return BoundArguments(self, arguments)

    def bind(self, *args, **kwargs):
        return self._bind(args, dict(kwargs))

    def bind_partial(self, *args, **kwargs):
        return self._bind(args, dict(kwargs), partial=True)

    def __str__(self):
        result = []
        render_pos_only_separator = False
        render_kw_only_separator = True
        for param in self._parameters.values():
            formatted = str(param)
            kind = param.kind
            if kind == _POSITIONAL_ONLY:
                render_pos_only_separator = True
            elif render_pos_only_separator:
                result.append('/')
                render_pos_only_separator = False
            if kind == _VAR_POSITIONAL:
                render_kw_only_separator = False
            elif kind == _KEYWORD_ONLY and render_kw_only_separator:
                result.append('*')
                render_kw_only_separator = False
            result.append(formatted)
        if render_pos_only_separator:
            result.append('/')
        rendered = '({})'.format(', '.join(result))
        if self._return_annotation is not _empty:
            anno = formatannotation(self._return_annotation)
            rendered += ' -> {}'.format(anno)
        return rendered

    def __repr__(self):
        return '<{} {}>'.format(type(self).__name__, self)

    @classmethod
    def from_callable(cls, obj, *, follow_wrapped=True):
        return signature(obj, follow_wrapped=follow_wrapped)


def _kindname(k):
    return ['POSITIONAL_ONLY', 'POSITIONAL_OR_KEYWORD', 'VAR_POSITIONAL', 'KEYWORD_ONLY', 'VAR_KEYWORD'][int(k)]


def _sig_from_function(func, skip_first=0):
    co = func.__code__
    anns = getattr(func, '__annotations__', None) or {}
    defaults = func.__defaults__ or ()
    kwdefaults = getattr(func, '__kwdefaults__', None) or {}
    nargs = co.co_argcount
    nposonly = co.co_posonlyargcount
    nkwonly = co.co_kwonlyargcount
    names = co.co_varnames
    params = []
    ndef = len(defaults)
    for i in range(nargs):
        name = names[i]
        kind = _POSITIONAL_ONLY if i < nposonly else _POSITIONAL_OR_KEYWORD
        j = i - (nargs - ndef)
        default = defaults[j] if j >= 0 else _empty
        params.append(Parameter(name, kind, default=default, annotation=anns.get(name, _empty)))
    pos = nargs + nkwonly
    if co.co_flags & CO_VARARGS:
        name = names[pos]
        params.append(Parameter(name, _VAR_POSITIONAL, annotation=anns.get(name, _empty)))
        pos += 1
    for i in range(nargs, nargs + nkwonly):
        name = names[i]
        params.append(Parameter(name, _KEYWORD_ONLY, default=kwdefaults.get(name, _empty),
                                annotation=anns.get(name, _empty)))
    if co.co_flags & CO_VARKEYWORDS:
        name = names[pos]
        params.append(Parameter(name, _VAR_KEYWORD, annotation=anns.get(name, _empty)))
    if skip_first:
        params = params[skip_first:]
    return Signature(params, return_annotation=anns.get('return', _empty))


def signature(obj, *, follow_wrapped=True):
    if not callable(obj):
        raise TypeError('{!r} is not a callable object'.format(obj))
    if follow_wrapped:
        obj = unwrap(obj, stop=lambda f: hasattr(f, '__signature__'))
    sig = getattr(obj, '__signature__', None)
    if sig is not None:
        if not isinstance(sig, Signature):
            raise TypeError('unexpected object {!r} in __signature__ attribute'.format(sig))
        return sig
    if isinstance(obj, type):
        init = None
        for klass in obj.__mro__:
            if klass is object:
                break
            if '__init__' in klass.__dict__:
                init = klass.__dict__['__init__']
                break
        if init is not None and hasattr(init, '__code__'):
            return _sig_from_function(init, 1)
        for klass in obj.__mro__:
            if klass is object:
                break
            if '__new__' in klass.__dict__ and hasattr(klass.__dict__['__new__'], '__code__'):
                return _sig_from_function(klass.__dict__['__new__'], 1)
        if init is None:
            return Signature()
        raise ValueError('no signature found for builtin type %r' % (obj,))
    func = getattr(obj, '__func__', None)
    if func is not None and hasattr(func, '__code__'):
        return _sig_from_function(func, 1)
    if hasattr(obj, '__code__') and hasattr(obj, '__defaults__'):
        return _sig_from_function(obj)
    call = getattr(type(obj), '__call__', None)
    if call is not None and hasattr(call, '__code__'):
        return _sig_from_function(call, 1)
    raise ValueError('no signature found for %r' % (obj,))
