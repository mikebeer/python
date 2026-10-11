from .consts import PI, NAME as CONST_NAME
import sys

loads = 0
loads += 1


def add(a, b):
    return a + b


def area(r):
    return PI * r * r


def where():
    return __name__, CONST_NAME


class Counter:
    count = 0

    def __init__(self):
        Counter.count += 1
        self.id = Counter.count

    def __repr__(self):
        return "Counter(%d)" % self.id
