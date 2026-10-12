# Minimal select() for the CREXX socket module: polls rxsocket's pending-byte count.
import time as _time


def select(rlist, wlist, xlist, timeout=None):
    end = None
    if timeout is not None:
        end = _time.time() + timeout
    while True:
        r = []
        for s in rlist:
            if getattr(s, "_listening", False) or _sock(17, s.fileno()) > 0 or _sock(10, s.fileno()) == 1:
                r.append(s)
        if r or wlist or (end is not None and _time.time() >= end):
            return (r, list(wlist), [])
        if timeout == 0:
            return ([], [], [])
