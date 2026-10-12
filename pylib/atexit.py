# atexit: handlers are registered here; the interpreters do not call _run_exitfuncs() at exit by themselves,
# so call atexit._run_exitfuncs() explicitly (or end the program with it).
_handlers = []


def register(func, *args, **kwargs):
    _handlers.append((func, args, kwargs))
    return func


def unregister(func):
    global _handlers
    _handlers = [h for h in _handlers if h[0] != func]


def _run_exitfuncs():
    while _handlers:
        func, args, kwargs = _handlers.pop()
        try:
            func(*args, **kwargs)
        except SystemExit:
            pass
        except BaseException as e:
            import sys
            sys.stderr.write("Error in atexit._run_exitfuncs:\n%s: %s\n" % (type(e).__name__, e))


def _clear():
    del _handlers[:]


def _ncallbacks():
    return len(_handlers)
