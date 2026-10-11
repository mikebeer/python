# module import system: files, packages, relative imports, caching, star-import, cycles
import sys
import math
import pkg
import pkg.util
import pkg.sub.deep as deep
from pkg import add, Counter, VERSION
from pkg.util import area, where as util_where
from pkg.consts import *
from pkg import sub

print("main name:", __name__)
print(pkg.__name__, pkg.util.__name__, deep.__name__, sub.__name__)
print(add(2, 3), area(2), util_where())
print(VERSION, PI, NAME)
try:
    print(_private)
except NameError:
    print("_private not exported")

# caching: the same module object, executed once
import pkg.util as u2
print(u2 is pkg.util, pkg.util.loads, "pkg.util" in sys.modules, "pkg" in sys.modules)
print(sys.modules["pkg.sub.deep"] is deep)
c1, c2 = Counter(), Counter()
print(c1, c2, pkg.util.Counter.count)

# relative imports inside the package
print(deep.run(10))
print(deep.names())
print(deep.util is pkg.util, deep.sibling.TAG)

# __all__ controls "import *"
ns = {}
exec("from pkg import *", ns)
print(sorted(k for k in ns if not k.startswith("__")))
ns2 = {}
exec("from pkg.consts import *", ns2)
print(sorted(k for k in ns2 if not k.startswith("__")))

# circular imports
import pkg.circ_a
import pkg.circ_b
print(pkg.circ_a.get_b(), pkg.circ_b.get_a())

# native modules take precedence, errors
print(math.floor(2.5), "math" in sys.modules)
try:
    import no_such_module
except ImportError as e:
    print("ImportError:", type(e).__name__)
try:
    from pkg import no_such_name
except ImportError as e:
    print("ImportError:", type(e).__name__)
try:
    import pkg.no_such_sub
except ImportError as e:
    print("ImportError:", type(e).__name__)

# import inside a function
def late():
    import pkg.consts as k
    from pkg.sub.sibling import twice
    return k.NAME, twice(21)
print(late())
print(type(pkg).__name__, hasattr(pkg, "util"), hasattr(pkg, "nothing"))
if __name__ == "__main__":
    print("running as script")
