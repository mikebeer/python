# sys.executable is an absolute path; dir() of built-in values lists their methods
import os
import sys

print(isinstance(sys.executable, str), os.path.isabs(sys.executable))
for v in ([], (), {}, set(), "", 0, 0.5, b""):
    pub = [n for n in dir(v) if not n.startswith("_")]
    print(type(v).__name__, pub == sorted(pub), "count" in pub or "keys" in pub or "add" in pub)
print([n for n in dir({}) if not n.startswith("_")])
print([n for n in dir([]) if not n.startswith("_")])
print([n for n in dir(set()) if not n.startswith("_")])
print([n for n in dir(()) if not n.startswith("_")])
print("upper" in dir("x"), "append" in dir([]), "nope" in dir([]))
