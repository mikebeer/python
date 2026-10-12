"""collections.abc: abstract base classes as plain classes (no registration of builtin types:
isinstance(x, Mapping) is true only for classes deriving from these)."""
from abc import ABC, abstractmethod

__all__ = ["Awaitable", "Coroutine", "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Hashable", "Iterable",
           "Iterator", "Generator", "Reversible", "Sized", "Container", "Callable", "Collection", "Set", "MutableSet",
           "Mapping", "MutableMapping", "MappingView", "KeysView", "ItemsView", "ValuesView", "Sequence",
           "MutableSequence", "ByteString", "Buffer"]


class Hashable:
    pass


class Awaitable:
    pass


class Coroutine(Awaitable):
    pass


class AsyncIterable:
    pass


class AsyncIterator(AsyncIterable):
    pass


class AsyncGenerator(AsyncIterator):
    pass


class Iterable:
    def __iter__(self):
        while False:
            yield None


class Iterator(Iterable):
    def __iter__(self):
        return self


class Reversible(Iterable):
    pass


class Generator(Iterator):
    pass


class Sized:
    pass


class Container:
    pass


class Callable:
    pass


class Collection(Sized, Iterable, Container):
    pass


class Buffer:
    pass


class Set(Collection):
    def __le__(self, other):
        if len(self) > len(other):
            return False
        for elem in self:
            if elem not in other:
                return False
        return True

    def __lt__(self, other):
        return len(self) < len(other) and self.__le__(other)

    def __gt__(self, other):
        return len(self) > len(other) and self.__ge__(other)

    def __ge__(self, other):
        if len(self) < len(other):
            return False
        for elem in other:
            if elem not in self:
                return False
        return True

    def __eq__(self, other):
        return len(self) == len(other) and self.__le__(other)

    def isdisjoint(self, other):
        for value in other:
            if value in self:
                return False
        return True


class MutableSet(Set):
    def clear(self):
        try:
            while True:
                self.pop()
        except KeyError:
            pass

    def pop(self):
        it = iter(self)
        try:
            value = next(it)
        except StopIteration:
            raise KeyError from None
        self.discard(value)
        return value

    def remove(self, value):
        if value not in self:
            raise KeyError(value)
        self.discard(value)


class Mapping(Collection):
    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    def __contains__(self, key):
        try:
            self[key]
        except KeyError:
            return False
        else:
            return True

    def keys(self):
        return list(self)

    def items(self):
        return [(key, self[key]) for key in self]

    def values(self):
        return [self[key] for key in self]

    def __eq__(self, other):
        if not isinstance(other, Mapping):
            return NotImplemented
        return dict(self.items()) == dict(other.items())


class MappingView(Sized):
    def __init__(self, mapping):
        self._mapping = mapping

    def __len__(self):
        return len(self._mapping)


class KeysView(MappingView, Set):
    pass


class ItemsView(MappingView, Set):
    pass


class ValuesView(MappingView, Collection):
    pass


class MutableMapping(Mapping):
    __marker = object()

    def pop(self, key, default=__marker):
        try:
            value = self[key]
        except KeyError:
            if default is MutableMapping.__marker:
                raise
            return default
        else:
            del self[key]
            return value

    def popitem(self):
        try:
            key = next(iter(self))
        except StopIteration:
            raise KeyError from None
        value = self[key]
        del self[key]
        return key, value

    def clear(self):
        try:
            while True:
                self.popitem()
        except KeyError:
            pass

    def update(self, other=(), **kwds):
        if isinstance(other, Mapping):
            for key in other:
                self[key] = other[key]
        elif hasattr(other, "keys"):
            for key in other.keys():
                self[key] = other[key]
        else:
            for key, value in other:
                self[key] = value
        for key, value in kwds.items():
            self[key] = value

    def setdefault(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            self[key] = default
        return default


class Sequence(Reversible, Collection):
    def __iter__(self):
        i = 0
        try:
            while True:
                v = self[i]
                yield v
                i += 1
        except IndexError:
            return

    def __contains__(self, value):
        for v in self:
            if v is value or v == value:
                return True
        return False

    def __reversed__(self):
        for i in reversed(range(len(self))):
            yield self[i]

    def index(self, value, start=0, stop=None):
        if start is not None and start < 0:
            start = max(len(self) + start, 0)
        if stop is not None and stop < 0:
            stop += len(self)
        i = start
        while stop is None or i < stop:
            try:
                v = self[i]
            except IndexError:
                break
            if v is value or v == value:
                return i
            i += 1
        raise ValueError

    def count(self, value):
        return sum(1 for v in self if v is value or v == value)


class MutableSequence(Sequence):
    def append(self, value):
        self.insert(len(self), value)

    def clear(self):
        try:
            while True:
                self.pop()
        except IndexError:
            pass

    def reverse(self):
        n = len(self)
        for i in range(n // 2):
            self[i], self[n - i - 1] = self[n - i - 1], self[i]

    def extend(self, values):
        if values is self:
            values = list(values)
        for v in values:
            self.append(v)

    def pop(self, index=-1):
        v = self[index]
        del self[index]
        return v

    def remove(self, value):
        del self[self.index(value)]

    def __iadd__(self, values):
        self.extend(values)
        return self


class ByteString(Sequence):
    pass
