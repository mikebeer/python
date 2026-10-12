# concurrent.futures with synchronous (cooperative) execution: submit() runs the callable immediately.
import threading as _threading

FIRST_COMPLETED = 'FIRST_COMPLETED'
FIRST_EXCEPTION = 'FIRST_EXCEPTION'
ALL_COMPLETED = 'ALL_COMPLETED'


class Error(Exception):
    pass


class CancelledError(Error):
    pass


class TimeoutError(Error):
    pass


class InvalidStateError(Error):
    pass


class BrokenExecutor(RuntimeError):
    pass


class Future:
    def __init__(self):
        self._state = 'PENDING'
        self._result = None
        self._exception = None
        self._callbacks = []

    def cancel(self):
        if self._state in ('RUNNING', 'FINISHED'):
            return False
        self._state = 'CANCELLED'
        self._fire()
        return True

    def cancelled(self):
        return self._state == 'CANCELLED'

    def running(self):
        return self._state == 'RUNNING'

    def done(self):
        return self._state in ('CANCELLED', 'FINISHED')

    def result(self, timeout=None):
        if self._state == 'CANCELLED':
            raise CancelledError()
        if self._state != 'FINISHED':
            raise TimeoutError()
        if self._exception is not None:
            raise self._exception
        return self._result

    def exception(self, timeout=None):
        if self._state == 'CANCELLED':
            raise CancelledError()
        if self._state != 'FINISHED':
            raise TimeoutError()
        return self._exception

    def add_done_callback(self, fn):
        if self.done():
            fn(self)
        else:
            self._callbacks.append(fn)

    def _fire(self):
        cbs = self._callbacks
        self._callbacks = []
        for cb in cbs:
            try:
                cb(self)
            except Exception:
                pass

    def set_result(self, result):
        if self._state == 'FINISHED':
            raise InvalidStateError('FINISHED: %r' % self)
        self._result = result
        self._state = 'FINISHED'
        self._fire()

    def set_exception(self, exception):
        if self._state == 'FINISHED':
            raise InvalidStateError('FINISHED: %r' % self)
        self._exception = exception
        self._state = 'FINISHED'
        self._fire()

    def set_running_or_notify_cancel(self):
        if self._state == 'CANCELLED':
            return False
        self._state = 'RUNNING'
        return True

    def __repr__(self):
        st = self._state.lower()
        if self._state == 'FINISHED':
            if self._exception is not None:
                st = 'finished raised ' + type(self._exception).__name__
            else:
                st = 'finished returned ' + type(self._result).__name__
        return '<Future at 0x7f0000000000 state=%s>' % st


class Executor:
    def submit(self, fn, /, *args, **kwargs):
        raise NotImplementedError()

    def map(self, fn, *iterables, timeout=None, chunksize=1):
        futs = [self.submit(fn, *args) for args in zip(*iterables)]

        def gen():
            for f in futs:
                yield f.result()
        return gen()

    def shutdown(self, wait=True, *, cancel_futures=False):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.shutdown(wait=True)
        return False


class ThreadPoolExecutor(Executor):
    def __init__(self, max_workers=None, thread_name_prefix='', initializer=None, initargs=()):
        if max_workers is not None and max_workers <= 0:
            raise ValueError("max_workers must be greater than 0")
        self._max_workers = max_workers if max_workers is not None else 8
        self._shutdown = False
        self._prefix = thread_name_prefix
        self._count = 0
        self._initializer = initializer
        self._initargs = initargs
        self._inited = False

    def submit(self, fn, /, *args, **kwargs):
        if self._shutdown:
            raise RuntimeError('cannot schedule new futures after shutdown')
        if self._initializer is not None and not self._inited:
            self._inited = True
            self._initializer(*self._initargs)
        f = Future()
        f.set_running_or_notify_cancel()
        try:
            f.set_result(fn(*args, **kwargs))
        except BaseException as e:
            f.set_exception(e)
        return f

    def shutdown(self, wait=True, *, cancel_futures=False):
        self._shutdown = True


class ProcessPoolExecutor(ThreadPoolExecutor):
    pass


def as_completed(fs, timeout=None):
    fs = list(fs)
    seen = []
    for f in fs:
        if f not in seen:
            seen.append(f)
    for f in seen:
        yield f


class _DoneAndNotDoneFutures:
    pass


def wait(fs, timeout=None, return_when=ALL_COMPLETED):
    done = set()
    not_done = set()
    for f in fs:
        if f.done():
            done.add(f)
        else:
            not_done.add(f)
    return (done, not_done)
