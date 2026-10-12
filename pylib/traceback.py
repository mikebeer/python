# traceback for interpreters without frame objects: exception text formatting is exact, stack information
# is not available (tracebacks are empty).
import sys as _sys

__all__ = ['extract_stack', 'extract_tb', 'format_exception', 'format_exception_only', 'format_list',
           'format_stack', 'format_tb', 'print_exc', 'format_exc', 'print_exception', 'print_last', 'print_stack',
           'print_tb', 'clear_frames', 'FrameSummary', 'StackSummary', 'TracebackException', 'walk_stack',
           'walk_tb', 'print_list']


def print_list(extracted_list, file=None):
    if file is None:
        file = _sys.stderr
    for item in StackSummary.from_list(extracted_list).format():
        file.write(item)


def format_list(extracted_list):
    return StackSummary.from_list(extracted_list).format()


def print_tb(tb, limit=None, file=None):
    print_list(extract_tb(tb, limit=limit), file=file)


def format_tb(tb, limit=None):
    return extract_tb(tb, limit=limit).format()


def extract_tb(tb, limit=None):
    return StackSummary()


def walk_stack(f):
    return iter(())


def walk_tb(tb):
    return iter(())


def extract_stack(f=None, limit=None):
    return StackSummary()


def format_stack(f=None, limit=None):
    return []


def print_stack(f=None, limit=None, file=None):
    pass


def clear_frames(tb):
    pass


class FrameSummary:
    def __init__(self, filename, lineno, name, *, lookup_line=True, locals=None, line=None, end_lineno=None,
                 colno=None, end_colno=None):
        self.filename = filename
        self.lineno = lineno
        self.name = name
        self._line = line
        self.locals = None

    @property
    def line(self):
        return self._line

    def __eq__(self, other):
        if isinstance(other, FrameSummary):
            return (self.filename == other.filename and self.lineno == other.lineno and self.name == other.name)
        if isinstance(other, tuple):
            return (self.filename, self.lineno, self.name, self.line) == other
        return NotImplemented

    def __iter__(self):
        return iter([self.filename, self.lineno, self.name, self.line])

    def __getitem__(self, pos):
        return (self.filename, self.lineno, self.name, self.line)[pos]

    def __repr__(self):
        return "<FrameSummary file {filename}, line {lineno} in {name}>".format(filename=self.filename, lineno=self.lineno, name=self.name)

    def __len__(self):
        return 4


class StackSummary(list):
    @classmethod
    def extract(klass, frame_gen, *, limit=None, lookup_lines=True, capture_locals=False):
        return klass()

    @classmethod
    def from_list(klass, a_list):
        result = StackSummary()
        for frame in a_list:
            if isinstance(frame, FrameSummary):
                result.append(frame)
            else:
                filename, lineno, name, line = frame
                result.append(FrameSummary(filename, lineno, name, line=line))
        return result

    def format_frame_summary(self, frame_summary):
        row = ['  File "{}", line {}, in {}\n'.format(frame_summary.filename, frame_summary.lineno, frame_summary.name)]
        if frame_summary.line:
            row.append('    {}\n'.format(frame_summary.line.strip()))
        return ''.join(row)

    def format(self):
        return [self.format_frame_summary(f) for f in self]


def _exc_info():
    f = getattr(_sys, 'exc_info', None)
    if f is None:
        return (None, None, None)
    return f()


def _format_final_exc_line(etype, value):
    valuestr = _some_str(value)
    if value is None or not valuestr:
        line = "%s\n" % etype
    else:
        line = "%s: %s\n" % (etype, valuestr)
    return line


def _some_str(value):
    try:
        return str(value)
    except Exception:
        return '<exception str() failed>'


def _type_name(exc_type):
    n = getattr(exc_type, '__qualname__', None) or exc_type.__name__
    m = getattr(exc_type, '__module__', None)
    if m and m not in ('__main__', 'builtins') and isinstance(m, str):
        n = m + '.' + n
    return n


def format_exception_only(exc, /, value=None, *, show_group=False):
    if value is None:
        if isinstance(exc, BaseException):
            value = exc
            exc = type(value)
    if exc is None:
        return ["NoneType: None\n"]
    if isinstance(exc, type) and issubclass(exc, SyntaxError) and value is not None:
        return [_format_final_exc_line(_type_name(exc), value)]
    return [_format_final_exc_line(_type_name(exc), value)]


class TracebackException:
    def __init__(self, exc_type, exc_value, exc_traceback, *, limit=None, lookup_lines=True, capture_locals=False,
                 compact=False, max_group_width=15, max_group_depth=10, _seen=None):
        self.exc_type = exc_type
        self._exc_value = exc_value
        self.stack = StackSummary()
        self.__cause__ = None
        self.__context__ = None
        self.__suppress_context__ = False
        self.exceptions = None
        self._str = _some_str(exc_value)
        self.exc_type_str = _type_name(exc_type) if exc_type is not None else None

    @classmethod
    def from_exception(cls, exc, *args, **kwargs):
        return cls(type(exc), exc, None, *args, **kwargs)

    def format_exception_only(self):
        yield _format_final_exc_line(self.exc_type_str, self._exc_value if self._str else None)

    def format(self, *, chain=True):
        if self.stack:
            yield 'Traceback (most recent call last):\n'
            for s in self.stack.format():
                yield s
        for line in self.format_exception_only():
            yield line

    def print(self, *, file=None, chain=True):
        if file is None:
            file = _sys.stderr
        for line in self.format(chain=chain):
            file.write(line)


def format_exception(exc, /, value=None, tb=None, limit=None, chain=True):
    if value is None and isinstance(exc, BaseException):
        value = exc
        exc = type(value)
    return list(TracebackException(exc, value, tb).format(chain=chain))


def print_exception(exc, /, value=None, tb=None, limit=None, file=None, chain=True):
    if file is None:
        file = _sys.stderr
    for line in format_exception(exc, value, tb, limit, chain):
        file.write(line)


def format_exc(limit=None, chain=True):
    t, v, tb = _exc_info()
    if t is None:
        return "NoneType: None\n"
    return "".join(format_exception(t, v, tb, limit, chain))


def print_exc(limit=None, file=None, chain=True):
    if file is None:
        file = _sys.stderr
    file.write(format_exc(limit, chain))


def print_last(limit=None, file=None, chain=True):
    pass
