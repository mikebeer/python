# os and time: directory functions, process ids, urandom, struct_time, strftime/strptime round trips
import os, time, random, tempfile

print(hasattr(os, 'listdir'), hasattr(os, 'getpid'), hasattr(os, 'urandom'), hasattr(os, 'mkdir'))
print(os.getpid() > 0, os.getppid() >= 0, os.getpid() == os.getpid())
print(len(os.urandom(8)), type(os.urandom(3)).__name__, os.urandom(0))
print(os.fspath('a/b'), os.fsencode('héllo'), os.fsdecode(b'x/y'))

base = tempfile.mkdtemp()
try:
    top = os.path.join(base, 'top')
    os.mkdir(top)
    os.makedirs(os.path.join(top, 'a', 'b'), exist_ok=True)
    os.makedirs(os.path.join(top, 'a', 'b'), exist_ok=True)
    for name in ('f1.txt', 'a/f2.txt', 'a/b/f3.txt'):
        with open(os.path.join(top, name), 'w') as fh:
            fh.write(name)
    print(sorted(os.listdir(top)))
    print(sorted(os.listdir(os.path.join(top, 'a'))))
    for root, dirs, files in os.walk(top):
        dirs.sort()
        print(root[len(top):].lstrip('/') or '.', dirs, sorted(files))
    try:
        os.mkdir(top)
    except FileExistsError as e:
        print('exists', e.errno)
    try:
        os.listdir(os.path.join(base, 'missing'))
    except FileNotFoundError as e:
        print('missing', e.errno, e.filename == os.path.join(base, 'missing'))
    try:
        os.rmdir(os.path.join(top, 'a'))
    except OSError as e:
        print('notempty', e.errno)
    os.remove(os.path.join(top, 'a', 'b', 'f3.txt'))
    os.rmdir(os.path.join(top, 'a', 'b'))
    print(sorted(os.listdir(os.path.join(top, 'a'))))
finally:
    import shutil
    shutil.rmtree(base)
print(os.path.exists(base))

# time
t = time.gmtime(0)
print(t)
print(t.tm_year, t.tm_mon, t.tm_mday, t.tm_wday, t.tm_yday, t[0], len(t), tuple(t)[:3])
print(time.gmtime(1700000000))
print(time.gmtime(-86400 * 366))
print(time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(1700000000)))
print(time.strftime('%a %b %d %j %U %w %y %%', time.gmtime(1700000000)))
print(time.strftime('%A %B %I:%M %p', time.gmtime(1700000000)))
print(time.asctime(time.gmtime(1700000000)))
print(time.asctime((2024, 2, 29, 12, 30, 5, 3, 60, 0)))
st = time.strptime('2024-03-05 14:07:09', '%Y-%m-%d %H:%M:%S')
print(st.tm_year, st.tm_mon, st.tm_mday, st.tm_hour, st.tm_min, st.tm_sec, st.tm_wday, st.tm_yday)
print(time.strptime('05/Mar/24', '%d/%b/%y')[:3])
lt = time.localtime(1700000000)
print(type(lt).__name__, len(lt), lt.tm_year)
secs = time.mktime(lt)
print(secs == 1700000000.0, time.localtime(secs) == lt)
print(time.mktime(time.struct_time((2024, 1, 1, 0, 0, 0, 0, 1, -1))) == time.mktime(time.localtime(
    time.mktime(time.struct_time((2024, 1, 1, 0, 0, 0, 0, 1, -1))))))
print(time.struct_time((2001, 2, 3, 4, 5, 6, 7, 8, 9)).tm_isdst)
print(isinstance(time.ctime(0), str), len(time.ctime(0)))
print(time.strftime('%H:%M', time.struct_time((2000, 1, 1, 7, 5, 0, 5, 1, 0))))
print(time.time() > 1.6e9, time.monotonic() >= 0, time.perf_counter() >= 0)

# random with the operating system source
r = random.SystemRandom()
print(0 <= r.random() < 1, 1 <= r.randint(1, 6) <= 6, len(r.randbytes(4)), r.choice('abc') in 'abc')
random.seed(7)
s = random.getstate()
a = [random.random() for _ in range(3)]
random.setstate(s)
print(a == [random.random() for _ in range(3)])
