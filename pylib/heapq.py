"""heapq: heap queue algorithm (same sift algorithms as CPython, so heap layouts agree)."""


def _siftdown(heap, startpos, pos):
    newitem = heap[pos]
    while pos > startpos:
        parentpos = (pos - 1) >> 1
        parent = heap[parentpos]
        if newitem < parent:
            heap[pos] = parent
            pos = parentpos
            continue
        break
    heap[pos] = newitem


def _siftup(heap, pos):
    endpos = len(heap)
    startpos = pos
    newitem = heap[pos]
    childpos = 2 * pos + 1
    while childpos < endpos:
        rightpos = childpos + 1
        if rightpos < endpos and not heap[childpos] < heap[rightpos]:
            childpos = rightpos
        heap[pos] = heap[childpos]
        pos = childpos
        childpos = 2 * pos + 1
    heap[pos] = newitem
    _siftdown(heap, startpos, pos)


def heappush(heap, item):
    heap.append(item)
    _siftdown(heap, 0, len(heap) - 1)


def heappop(heap):
    if not heap:
        raise IndexError("index out of range")
    lastelt = heap.pop()
    if heap:
        returnitem = heap[0]
        heap[0] = lastelt
        _siftup(heap, 0)
        return returnitem
    return lastelt


def heapreplace(heap, item):
    if not heap:
        raise IndexError("index out of range")
    returnitem = heap[0]
    heap[0] = item
    _siftup(heap, 0)
    return returnitem


def heappushpop(heap, item):
    if heap and heap[0] < item:
        item, heap[0] = heap[0], item
        _siftup(heap, 0)
    return item


def heapify(x):
    n = len(x)
    for i in range(n // 2 - 1, -1, -1):
        _siftup(x, i)


def nlargest(n, iterable, key=None):
    return sorted(iterable, key=key, reverse=True)[:n]


def nsmallest(n, iterable, key=None):
    return sorted(iterable, key=key)[:n]


class _Merge:
    # not a generator function: generators of imported modules cannot see module globals on one interpreter
    def __init__(self, iterables, key, reverse):
        self.key = key
        self.reverse = reverse
        self.h = []
        order = 0
        for it in iterables:
            it = iter(it)
            for v in it:
                k = v if key is None else key(v)
                self.h.append([k, order, v, it])
                order += 1
                break
        if not reverse:
            heapify(self.h)

    def __iter__(self):
        return self

    def __next__(self):
        h = self.h
        if not h:
            raise StopIteration
        if self.reverse:
            bi = 0
            for i in range(1, len(h)):
                if h[i][0] > h[bi][0]:
                    bi = i
        else:
            bi = 0
        e = h[bi]
        res = e[2]
        nxt = False
        key = self.key
        for v in e[3]:
            e[0] = v if key is None else key(v)
            e[2] = v
            nxt = True
            break
        if self.reverse:
            if not nxt:
                del h[bi]
        elif nxt:
            _siftup(h, 0)
        else:
            heappop(h)
        return res


def merge(*iterables, key=None, reverse=False):
    return _Merge(iterables, key, reverse)


_heappop_max = None
