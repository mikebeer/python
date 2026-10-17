import os, sys, tempfile
d = tempfile.mkdtemp()
d2 = tempfile.mkdtemp()
with open(os.path.join(d, "rt_mod_a.py"), "w") as f:
    f.write("NAME = 'a'\n")
with open(os.path.join(d2, "rt_mod_b.py"), "w") as f:
    f.write("NAME = 'b'\n")
os.mkdir(os.path.join(d2, "rt_pkg"))
with open(os.path.join(d2, "rt_pkg", "__init__.py"), "w") as f:
    f.write("VALUE = 7\n")
try:
    import rt_mod_a
except ImportError:
    print("not yet")
sys.path.insert(0, d)
import rt_mod_a
print(rt_mod_a.NAME)
sys.path.append(d2)
import rt_mod_b
import rt_pkg
print(rt_mod_b.NAME, rt_pkg.VALUE)
print(sys.path[0] == d, sys.path[-1] == d2)
sys.path.remove(d2)
try:
    import rt_never
except ImportError:
    print("still not found")
sys.path = [d2] + sys.path
print(sys.path[0] == d2)
for p in (d, d2):
    for root, dirs, files in os.walk(p, topdown=False):
        for n in files:
            os.remove(os.path.join(root, n))
        for n in dirs:
            os.rmdir(os.path.join(root, n))
    os.rmdir(p)
