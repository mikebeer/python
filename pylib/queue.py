# Pure Python queue (cooperative: blocking get/put on a queue that cannot progress raises Empty/Full
# after running pending threads; see threading)
import heapq as _heapq
from collections import deque as _deque
import threading as _threading


class Empty(Exception):
    pass


class Full(Exception):
    pass


class ShutDown(Exception):
    pass


class Queue:
    def __init__(self, maxsize=0):
        self.maxsize = maxsize
        self._init(maxsize)
        self.unfinished_tasks = 0
        self.is_shutdown = False

    def _init(self, maxsize):
        self.queue = _deque()

    def _qsize(self):
        return len(self.queue)

    def _put(self, item):
        self.queue.append(item)

    def _get(self):
        return self.queue.popleft()

    def task_done(self):
        if self.unfinished_tasks <= 0:
            raise ValueError('task_done() called too many times')
        self.unfinished_tasks -= 1

    def join(self):
        if self.unfinished_tasks:
            _threading._block_until(lambda: self.unfinished_tasks == 0)
        if self.unfinished_tasks:
            raise RuntimeError("deadlock: Queue.join() would block forever (cooperative threading)")

    def qsize(self):
        return self._qsize()

    def empty(self):
        return not self._qsize()

    def full(self):
        return 0 < self.maxsize <= self._qsize()

    def put(self, item, block=True, timeout=None):
        if self.is_shutdown:
            raise ShutDown
        if self.maxsize > 0:
            if not block:
                if self._qsize() >= self.maxsize:
                    raise Full
            else:
                _threading._block_until(lambda: self._qsize() < self.maxsize)
                if self._qsize() >= self.maxsize:
                    if timeout is None:
                        raise RuntimeError("deadlock: Queue.put() would block forever (cooperative threading)")
                    raise Full
        self._put(item)
        self.unfinished_tasks += 1

    def get(self, block=True, timeout=None):
        if self.is_shutdown and not self._qsize():
            raise ShutDown
        if not self._qsize():
            if not block:
                raise Empty
            _threading._block_until(lambda: self._qsize() > 0)
            if not self._qsize():
                if timeout is None:
                    raise RuntimeError("deadlock: Queue.get() would block forever (cooperative threading)")
                raise Empty
        item = self._get()
        return item

    def put_nowait(self, item):
        return self.put(item, block=False)

    def get_nowait(self):
        return self.get(block=False)

    def shutdown(self, immediate=False):
        self.is_shutdown = True
        if immediate:
            while self._qsize():
                self._get()
                if self.unfinished_tasks > 0:
                    self.unfinished_tasks -= 1


class PriorityQueue(Queue):
    def _init(self, maxsize):
        self.queue = []

    def _qsize(self):
        return len(self.queue)

    def _put(self, item):
        _heapq.heappush(self.queue, item)

    def _get(self):
        return _heapq.heappop(self.queue)


class LifoQueue(Queue):
    def _init(self, maxsize):
        self.queue = []

    def _qsize(self):
        return len(self.queue)

    def _put(self, item):
        self.queue.append(item)

    def _get(self):
        return self.queue.pop()


class SimpleQueue:
    def __init__(self):
        self._q = _deque()

    def put(self, item, block=True, timeout=None):
        self._q.append(item)

    def put_nowait(self, item):
        self._q.append(item)

    def get(self, block=True, timeout=None):
        if not self._q:
            if not block:
                raise Empty
            _threading._block_until(lambda: len(self._q) > 0)
            if not self._q:
                if timeout is None:
                    raise RuntimeError("deadlock: SimpleQueue.get() would block forever (cooperative threading)")
                raise Empty
        return self._q.popleft()

    def get_nowait(self):
        return self.get(False)

    def empty(self):
        return not self._q

    def qsize(self):
        return len(self._q)
