import threading, queue, sched
from concurrent.futures import ThreadPoolExecutor, as_completed, Future, wait

results = []
lock = threading.Lock()
def worker(n):
    with lock:
        results.append(n * n)
ts = [threading.Thread(target=worker, args=(i,)) for i in range(5)]
for t in ts:
    t.start()
for t in ts:
    t.join()
print(sorted(results), [t.is_alive() for t in ts], threading.current_thread().name, threading.active_count())

q = queue.Queue()
out = []
def consumer():
    while True:
        item = q.get()
        if item is None:
            break
        out.append(item * 2)
        q.task_done()
def producer():
    for i in range(5):
        q.put(i)
    q.put(None)
c = threading.Thread(target=consumer)
p = threading.Thread(target=producer)
c.start(); p.start()
p.join(); c.join()
print(out)

ev = threading.Event()
got = []
t = threading.Thread(target=lambda: (ev.wait(), got.append('go')))
t.start(); ev.set(); t.join()
print(got, ev.is_set())

q2 = queue.Queue(maxsize=2)
q2.put(1); q2.put(2)
try:
    q2.put_nowait(3)
except queue.Full:
    print('Full', q2.full(), q2.qsize())
print(q2.get(), q2.get(), q2.empty())
try:
    q2.get_nowait()
except queue.Empty:
    print('Empty')
pq = queue.PriorityQueue()
for x in [5, 1, 3]:
    pq.put((x, str(x)))
print([pq.get() for _ in range(3)])
lq = queue.LifoQueue()
for x in [1, 2, 3]:
    lq.put(x)
print([lq.get() for _ in range(3)])
sq = queue.SimpleQueue(); sq.put('a'); print(sq.get(), sq.empty())

cond = threading.Condition()
box = []
def cons():
    with cond:
        cond.wait_for(lambda: len(box) > 0)
        print('consumed', box.pop())
def prod():
    with cond:
        box.append(42)
        cond.notify()
a = threading.Thread(target=cons); b = threading.Thread(target=prod)
a.start(); b.start(); a.join(); b.join()

class MyThread(threading.Thread):
    def __init__(self, n):
        threading.Thread.__init__(self, name='my-%d' % n)
        self.n = n
        self.result = None
    def run(self):
        self.result = self.n + 1
m = MyThread(3); m.start(); m.join(); print(m.name, m.result, m.is_alive())
sem = threading.Semaphore(2); sem.acquire(); sem.acquire(); print(sem.acquire(False)); sem.release(); print(sem.acquire(False))
rl = threading.RLock()
with rl:
    with rl:
        print('rlock ok')
print(lock.acquire(), lock.locked(), lock.acquire(False)); lock.release(); print(lock.locked())

with ThreadPoolExecutor(max_workers=3) as ex:
    fs = [ex.submit(pow, 2, i) for i in range(5)]
    print([f.result() for f in fs], list(ex.map(lambda x: x + 1, [1, 2, 3])), list(ex.map(pow, [2, 3], [3, 2])))
    bad = ex.submit(lambda: 1 / 0)
    print(type(bad.exception()).__name__, bad.done())
    try:
        bad.result()
    except ZeroDivisionError as e:
        print('ZDE', e)
    print(sorted(f.result() for f in as_completed(fs)), wait(fs)[1])
f = Future(); f.add_done_callback(lambda fut: print('cb', fut.result())); f.set_result(7); print(f.done(), f.result())

events = []
clock = [0]
s = sched.scheduler(lambda: clock[0], lambda d: clock.__setitem__(0, clock[0] + d))
s.enterabs(5, 1, events.append, ('b',)); s.enterabs(1, 1, events.append, ('a',)); s.enterabs(5, 0, events.append, ('c',))
s.run(); print(events)
tm = threading.Timer(0.01, events.append, ['timer']); tm.start(); tm.join(); print(events[-1])
b = threading.Barrier(2)
res = []
def bw(n):
    b.wait(); res.append(n)
t1 = threading.Thread(target=bw, args=(1,)); t2 = threading.Thread(target=bw, args=(2,))
t1.start(); t2.start(); t1.join(); t2.join(); print(sorted(res))
