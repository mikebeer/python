# Cooperative threading for interpreters without real threads.
# Thread.start() only registers the thread; its target runs to completion (in registration order) when the
# program joins it, or blocks on something another pending thread could satisfy (Event.wait, Queue.get,
# Lock.acquire on a held lock, Condition.wait, Semaphore.acquire, Barrier.wait), or calls
# threading.active_count()/enumerate()/is_alive().  Threads that are never joined and never waited for are
# not run unless threading.run_pending() is called.  Timeouts never really wait.
import time as _time

_pending = []
_idcounter = [0]
_current = []
TIMEOUT_MAX = 9223372036.0


def get_ident():
    if _current:
        return _current[-1].ident
    return 1


def get_native_id():
    return get_ident()


def _run_one():
    # run the first pending thread; return True if one ran
    if not _pending:
        return False
    t = _pending.pop(0)
    t._run_now()
    return True


def run_pending():
    while _run_one():
        pass


def _block_until(cond):
    # run pending threads until cond() is true
    while not cond():
        if not _run_one():
            return cond()
    return True


class ThreadError(RuntimeError):
    pass


class Thread:
    def __init__(self, group=None, target=None, name=None, args=(), kwargs=None, *, daemon=None):
        _idcounter[0] += 1
        self._n = _idcounter[0]
        self._target = target
        self._name = name if name is not None else "Thread-%d" % self._n
        self._args = args
        self._kwargs = kwargs if kwargs is not None else {}
        self.daemon = bool(daemon) if daemon is not None else False
        self._started = False
        self._done = False
        self._running = False
        self.ident = None
        self.native_id = None

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, v):
        self._name = str(v)

    def getName(self):
        return self._name

    def setName(self, n):
        self._name = str(n)

    def isDaemon(self):
        return self.daemon

    def setDaemon(self, d):
        self.daemon = d

    def start(self):
        if self._started:
            raise RuntimeError("threads can only be started once")
        self._started = True
        self.ident = self._n + 1
        self.native_id = self.ident
        _pending.append(self)

    def run(self):
        try:
            if self._target is not None:
                self._target(*self._args, **self._kwargs)
        finally:
            del self._target, self._args, self._kwargs

    def _run_now(self):
        if self._done or self._running:
            return
        self._running = True
        _current.append(self)
        try:
            self.run()
        except SystemExit:
            pass
        except BaseException as e:
            import sys as _sys
            hook = excepthook
            hook(ExceptHookArgs(type(e), e, None, self))
        finally:
            _current.pop()
            self._running = False
            self._done = True

    def join(self, timeout=None):
        if not self._started:
            raise RuntimeError("cannot join thread before it is started")
        if self is current_thread():
            raise RuntimeError("cannot join current thread")
        if self in _pending:
            _pending.remove(self)
            self._run_now()
        elif not self._done:
            _block_until(lambda: self._done)

    def is_alive(self):
        if self._started and not self._done:
            if self in _pending:
                _pending.remove(self)
                self._run_now()
                return False
            return True
        return False

    isAlive = is_alive

    def __repr__(self):
        st = "initial"
        if self._started:
            st = "stopped" if self._done else "started"
        return "<%s(%s, %s)>" % (type(self).__name__, self._name, st + (" daemon" if self.daemon else ""))


class ExceptHookArgs:
    def __init__(self, exc_type, exc_value, exc_traceback, thread):
        self.exc_type = exc_type
        self.exc_value = exc_value
        self.exc_traceback = exc_traceback
        self.thread = thread


def _default_excepthook(args):
    import sys as _sys
    name = args.thread.name if args.thread is not None else "?"
    _sys.stderr.write("Exception in thread %s:\n%s: %s\n" % (name, args.exc_type.__name__, args.exc_value))


excepthook = _default_excepthook


class _MainThread(Thread):
    def __init__(self):
        Thread.__init__(self, name="MainThread")
        self._started = True
        self.ident = 1
        self.native_id = 1


_main = _MainThread()


def main_thread():
    return _main


def current_thread():
    if _current:
        return _current[-1]
    return _main


currentThread = current_thread


def active_count():
    run_pending()
    return 1


activeCount = active_count


def enumerate():
    run_pending()
    return [_main]


class Lock:
    def __init__(self):
        self._locked = False

    def acquire(self, blocking=True, timeout=-1):
        if self._locked:
            if blocking:
                _block_until(lambda: not self._locked)
            if self._locked:
                if blocking and timeout < 0:
                    raise RuntimeError("deadlock: Lock.acquire() would block forever (cooperative threading)")
                return False
        self._locked = True
        return True

    def release(self):
        if not self._locked:
            raise RuntimeError("release unlocked lock")
        self._locked = False

    def locked(self):
        return self._locked

    def __enter__(self):
        self.acquire()
        return True

    def __exit__(self, a, b, c):
        self.release()
        return False


class RLock:
    def __init__(self):
        self._owner = None
        self._count = 0

    def acquire(self, blocking=True, timeout=-1):
        me = get_ident()
        if self._count and self._owner != me:
            if blocking:
                _block_until(lambda: self._count == 0)
            if self._count:
                if blocking and timeout < 0:
                    raise RuntimeError("deadlock: RLock.acquire() would block forever (cooperative threading)")
                return False
        self._owner = me
        self._count += 1
        return True

    def release(self):
        if self._count == 0 or self._owner != get_ident():
            raise RuntimeError("cannot release un-acquired lock")
        self._count -= 1
        if self._count == 0:
            self._owner = None

    def _is_owned(self):
        return self._count > 0 and self._owner == get_ident()

    def _release_save(self):
        st = (self._count, self._owner)
        self._count = 0
        self._owner = None
        return st

    def _acquire_restore(self, st):
        if self._count:
            _block_until(lambda: self._count == 0)
        self._count, self._owner = st

    def __enter__(self):
        self.acquire()
        return True

    def __exit__(self, a, b, c):
        self.release()
        return False


class Condition:
    def __init__(self, lock=None):
        self._lock = lock if lock is not None else RLock()
        self._waiters = 0
        self._notified = 0
        self.acquire = self._lock.acquire
        self.release = self._lock.release
        if hasattr(self._lock, '_release_save'):
            self._rs = self._lock._release_save
            self._ar = self._lock._acquire_restore
        else:
            self._rs = self._lock.release
            self._ar = lambda st: self._lock.acquire()

    def __enter__(self):
        return self._lock.__enter__()

    def __exit__(self, a, b, c):
        return self._lock.__exit__(a, b, c)

    def wait(self, timeout=None):
        self._waiters += 1
        mine = self._waiters
        st = self._rs()
        try:
            ok = _block_until(lambda: self._notified >= mine)
        finally:
            self._ar(st)
        return ok

    def wait_for(self, predicate, timeout=None):
        result = predicate()
        if not result:
            st = self._rs()
            try:
                _block_until(predicate)
            finally:
                self._ar(st)
            result = predicate()
        return result

    def notify(self, n=1):
        avail = self._waiters - self._notified
        self._notified += min(n, avail)

    def notify_all(self):
        self._notified = self._waiters

    notifyAll = notify_all


class Semaphore:
    def __init__(self, value=1):
        if value < 0:
            raise ValueError("semaphore initial value must be >= 0")
        self._value = value

    def acquire(self, blocking=True, timeout=None):
        if self._value <= 0 and blocking:
            _block_until(lambda: self._value > 0)
        if self._value <= 0:
            if blocking and timeout is None:
                raise RuntimeError("deadlock: Semaphore.acquire() would block forever (cooperative threading)")
            return False
        self._value -= 1
        return True

    __enter__ = acquire

    def release(self, n=1):
        self._value += n

    def __exit__(self, a, b, c):
        self.release()


class BoundedSemaphore(Semaphore):
    def __init__(self, value=1):
        Semaphore.__init__(self, value)
        self._initial_value = value

    def release(self, n=1):
        if self._value + n > self._initial_value:
            raise ValueError("Semaphore released too many times")
        self._value += n


class Event:
    def __init__(self):
        self._flag = False

    def is_set(self):
        return self._flag

    isSet = is_set

    def set(self):
        self._flag = True

    def clear(self):
        self._flag = False

    def wait(self, timeout=None):
        if not self._flag:
            _block_until(lambda: self._flag)
        return self._flag


class Barrier:
    def __init__(self, parties, action=None, timeout=None):
        self._parties = parties
        self._action = action
        self._count = 0
        self._gen = 0
        self.broken = False

    @property
    def parties(self):
        return self._parties

    @property
    def n_waiting(self):
        return self._count

    def wait(self, timeout=None):
        idx = self._count
        self._count += 1
        gen = self._gen
        if self._count == self._parties:
            if self._action:
                self._action()
            self._count = 0
            self._gen += 1
        else:
            _block_until(lambda: self._gen != gen)
        return idx

    def reset(self):
        self._count = 0
        self._gen += 1

    def abort(self):
        self.broken = True


class BrokenBarrierError(RuntimeError):
    pass


class Timer(Thread):
    def __init__(self, interval, function, args=None, kwargs=None):
        Thread.__init__(self)
        self.interval = interval
        self.function = function
        self.args = args if args is not None else []
        self.kwargs = kwargs if kwargs is not None else {}
        self.finished = Event()

    def cancel(self):
        self.finished.set()
        if self in _pending:
            _pending.remove(self)
            self._done = True

    def run(self):
        if not self.finished.is_set():
            self.function(*self.args, **self.kwargs)
        self.finished.set()


class local:
    pass


def settrace(f):
    pass


def setprofile(f):
    pass


def stack_size(size=0):
    return 0
