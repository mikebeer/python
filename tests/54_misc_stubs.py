import weakref, atexit, gc, traceback, secrets, getpass, platform, timeit, graphlib
class A: pass
a=A(); r=weakref.ref(a); print(r() is a)
d=weakref.WeakValueDictionary(); d['x']=a; print(len(d), 'x' in d)
ts=graphlib.TopologicalSorter({'b':['a'],'c':['a','b'],'d':['c']})
print(list(ts.static_order()))
try:
    t=graphlib.TopologicalSorter({'a':['b'],'b':['a']}); t.prepare()
except graphlib.CycleError as e: print('cycle', type(e).__name__)
atexit.register(print,'bye'); atexit._run_exitfuncs()
print(len(secrets.token_hex(8)), len(secrets.token_bytes(5)), len(secrets.token_urlsafe(6))>0)
print(platform.system(), gc.isenabled(), gc.collect())
print(traceback.format_exception_only(ValueError, ValueError('bad')))
try: 1/0
except Exception as e: print(traceback.format_exception_only(type(e), e))
print(timeit.timeit(lambda: None, number=10) >= 0)
print(secrets.compare_digest('a','a'))
