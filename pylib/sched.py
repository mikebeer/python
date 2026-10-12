# sched: event scheduler (delayfunc is called with the remaining delay)
import time as _time
import heapq as _heapq
from collections import namedtuple as _nt

Event = _nt('Event', 'time, priority, sequence, action, argument, kwargs')


class scheduler:
    def __init__(self, timefunc=_time.time, delayfunc=_time.sleep):
        self._queue = []
        self.timefunc = timefunc
        self.delayfunc = delayfunc
        self._seq = 0

    def enterabs(self, time, priority, action, argument=(), kwargs={}):
        self._seq += 1
        event = Event(time, priority, self._seq, action, argument, kwargs)
        _heapq.heappush(self._queue, event)
        return event

    def enter(self, delay, priority, action, argument=(), kwargs={}):
        return self.enterabs(self.timefunc() + delay, priority, action, argument, kwargs)

    def cancel(self, event):
        self._queue.remove(event)
        _heapq.heapify(self._queue)

    def empty(self):
        return not self._queue

    def run(self, blocking=True):
        q = self._queue
        delayfunc = self.delayfunc
        timefunc = self.timefunc
        pop = _heapq.heappop
        while q:
            time, priority, sequence, action, argument, kwargs = q[0]
            now = timefunc()
            if time > now:
                delay = True
            else:
                delay = False
                pop(q)
            if delay:
                if not blocking:
                    return time - now
                delayfunc(time - now)
            else:
                action(*argument, **kwargs)
                delayfunc(0)

    @property
    def queue(self):
        events = list(self._queue)
        events.sort()
        return events
