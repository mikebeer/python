# select() for the CREXX socket module on top of rxsocket.  A connected socket is readable when bytes are
# pending (or the peer closed); a listening socket is readable when a connection is waiting: the connection is
# accepted with a short timeout and kept inside the socket for the next accept().
import time as _time


def _ready(s, ms):
    if getattr(s, "_listening", False):
        return s._probe(ms)
    h = s.fileno()
    return _sock(17, h) > 0 or _sock(10, h) == 1


def select(rlist, wlist, xlist, timeout=None):
    end = None
    if timeout is not None:
        end = _time.time() + timeout
    rl = list(rlist)
    wl = list(wlist)
    first = True
    while True:
        r = []
        # a single listening socket and a timeout: let accept() do the waiting
        ms = 1
        if len(rl) == 1 and getattr(rl[0], "_listening", False) and not wl and not first:
            if end is None:
                ms = 100
            else:
                ms = max(1, int((end - _time.time()) * 1000))
                if ms > 100:
                    ms = 100
        for s in rl:
            if _ready(s, ms):
                r.append(s)
        if r or wl:
            return (r, wl, [])
        if timeout is not None and (timeout == 0 or _time.time() >= end):
            return ([], [], [])
        first = False
        if not (len(rl) == 1 and getattr(rl[0], "_listening", False)):
            _time.sleep(0.002)
