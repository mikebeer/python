"""warnings: minimal stub.  warn() prints 'Category: message' to stderr unless filtered."""
import sys

_filters = []          # list of (action, message_prefix, category)
_record = None         # list being filled while inside catch_warnings(record=True)
_seen = {}


class WarningMessage:
    def __init__(self, message, category, filename=None, lineno=0):
        self.message = message
        self.category = category
        self.filename = filename
        self.lineno = lineno

    def __str__(self):
        return str(self.message)


def simplefilter(action, category=Warning, lineno=0, append=False):
    _filters.insert(0, (action, "", category))


def filterwarnings(action, message="", category=Warning, module="", lineno=0, append=False):
    _filters.insert(0, (action, message, category))


def resetwarnings():
    del _filters[:]


def _action_for(text, category):
    for action, prefix, cat in _filters:
        if isinstance(category, type) and issubclass(category, cat) and text.startswith(prefix):
            return action
    return "default"


def warn(message, category=None, stacklevel=1, source=None):
    if isinstance(message, Warning):
        category = message.__class__
        text = str(message)
    else:
        text = message
        if category is None:
            category = UserWarning
    action = _action_for(text, category)
    if action == "ignore":
        return None
    if action == "error":
        raise category(text)
    if _record is not None:
        _record.append(WarningMessage(category(text), category))
        return None
    if action in ("default", "once", "module"):
        key = (text, category)
        if key in _seen:
            return None
        _seen[key] = 1
    sys.stderr.write("%s: %s\n" % (category.__name__, text))
    return None


def showwarning(message, category, filename, lineno, file=None, line=None):
    (file or sys.stderr).write("%s: %s\n" % (category.__name__, message))


class catch_warnings:
    def __init__(self, record=False, module=None):
        self._rec = record
        self._saved = None

    def __enter__(self):
        global _record
        self._saved = (list(_filters), _record)
        if self._rec:
            _record = []
            return _record
        return None

    def __exit__(self, exc_type, exc, tb):
        global _record
        _filters[:] = self._saved[0]
        _record = self._saved[1]
        return False
