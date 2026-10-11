"""small test package"""
print("init pkg", __name__)
from . import consts
from .util import add, Counter
VERSION = consts.VERSION
__all__ = ["add", "Counter", "VERSION", "consts"]
