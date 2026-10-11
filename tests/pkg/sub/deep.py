from .. import util
from ..consts import *
from . import sibling
from .sibling import twice


def run(x):
    return util.add(x, 1), twice(x), PI, NAME, sibling.TAG


def names():
    return sorted(n for n in globals() if not n.startswith("__"))
