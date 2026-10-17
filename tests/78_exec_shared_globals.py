# exec()/eval() chunks share one namespace: a function defined by an earlier chunk sees globals
# rebound by a later chunk (CPython: functions resolve globals in the shared dict at call time)
exec("g = 1\ndef f():\n    return g\n")
exec("g = 2")
print(f(), f() == 2)
exec("g += 5\nh = f() * 2")
print(g, h, f())

# an explicit dict is one persistent namespace too
ns = {}
exec("k = 10\ndef get():\n    return k\ndef bump():\n    global k\n    k += 1\n", ns)
exec("k = 20", ns)
print(ns["get"](), ns["k"])
exec("bump()\nbump()", ns)
print(ns["get"](), ns["k"])
exec("def later():\n    return get() + extra\nextra = 100", ns)
print(ns["later"]())
exec("extra = 1", ns)
print(ns["later"]())

# a name used before it is defined (by a function) is resolved at call time
exec("def early():\n    return late_name")
exec("late_name = 'ok'")
print(early())

# classes and methods see rebound globals
exec("scale = 2\nclass C:\n    def m(self, x):\n        return x * scale\n")
exec("scale = 7")
print(C().m(3))

# an exception in a chunk keeps the earlier assignments, and del removes a name
try:
    exec("part = 1\nboom()\n")
except NameError:
    pass
print(part)
exec("tmp = 3\ndel tmp")
try:
    exec("print(tmp)")
except NameError:
    print("tmp gone")

# eval sees the namespace of the earlier chunks, separate dicts stay separate
print(eval("g + h"))
a, b = {}, {}
exec("v = 1\ndef r():\n    return v", a)
exec("v = 2\ndef r():\n    return v", b)
exec("v = 3", a)
print(a["r"](), b["r"]())

# globals rebound inside a function that an earlier chunk defined are seen by later chunks
exec("cnt = 0\ndef tick():\n    global cnt\n    cnt += 1\n    return cnt\n")
exec("tick()")
exec("tick()")
print(eval("cnt"), eval("tick()"), eval("cnt"))
