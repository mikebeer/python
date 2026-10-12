# logging.handlers (subset): buffering, memory, queue and null-like handlers
import logging


class BufferingHandler(logging.Handler):
    def __init__(self, capacity):
        logging.Handler.__init__(self)
        self.capacity = capacity
        self.buffer = []

    def shouldFlush(self, record):
        return len(self.buffer) >= self.capacity

    def emit(self, record):
        self.buffer.append(record)
        if self.shouldFlush(record):
            self.flush()

    def flush(self):
        self.acquire()
        try:
            self.buffer.clear()
        finally:
            self.release()

    def close(self):
        try:
            self.flush()
        finally:
            logging.Handler.close(self)


class MemoryHandler(BufferingHandler):
    def __init__(self, capacity, flushLevel=logging.ERROR, target=None, flushOnClose=True):
        BufferingHandler.__init__(self, capacity)
        self.flushLevel = flushLevel
        self.target = target
        self.flushOnClose = flushOnClose

    def shouldFlush(self, record):
        return (len(self.buffer) >= self.capacity) or (record.levelno >= self.flushLevel)

    def setTarget(self, target):
        self.acquire()
        try:
            self.target = target
        finally:
            self.release()

    def flush(self):
        self.acquire()
        try:
            if self.target:
                for record in self.buffer:
                    self.target.handle(record)
                self.buffer.clear()
        finally:
            self.release()

    def close(self):
        try:
            if self.flushOnClose:
                self.flush()
        finally:
            self.acquire()
            try:
                self.target = None
                BufferingHandler.close(self)
            finally:
                self.release()


class QueueHandler(logging.Handler):
    def __init__(self, queue):
        logging.Handler.__init__(self)
        self.queue = queue

    def enqueue(self, record):
        self.queue.put_nowait(record)

    def prepare(self, record):
        msg = self.format(record)
        record.message = msg
        record.msg = msg
        record.args = None
        record.exc_info = None
        record.exc_text = None
        return record

    def emit(self, record):
        try:
            self.enqueue(self.prepare(record))
        except Exception:
            self.handleError(record)


class QueueListener:
    _sentinel = None

    def __init__(self, queue, *handlers, respect_handler_level=False):
        self.queue = queue
        self.handlers = handlers
        self.respect_handler_level = respect_handler_level

    def dequeue(self, block):
        return self.queue.get(block)

    def prepare(self, record):
        return record

    def handle(self, record):
        record = self.prepare(record)
        for handler in self.handlers:
            if not self.respect_handler_level:
                process = True
            else:
                process = record.levelno >= handler.level
            if process:
                handler.handle(record)

    def start(self):
        pass

    def stop(self):
        # synchronous: drain whatever is queued
        while not self.queue.empty():
            record = self.queue.get_nowait()
            if record is self._sentinel:
                continue
            self.handle(record)
