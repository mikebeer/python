import tempfile, shutil, glob, os
print(tempfile.gettempdir() != '', tempfile.gettempprefix())
n = tempfile.mktemp(suffix='.txt', prefix='pre_', dir='/tmp')
print(n.startswith('/tmp/pre_'), n.endswith('.txt'), len(n) == len('/tmp/pre_') + 8 + 4, os.path.exists(n))
with tempfile.NamedTemporaryFile('w', suffix='.dat', dir='/tmp') as f:
    f.write('hello\n')
    f.flush()
    name = f.name
    print(os.path.exists(name), name.endswith('.dat'))
print(os.path.exists(name))
fd, path = tempfile.mkstemp(dir='/tmp')
print(os.path.exists(path))
os.remove(path)
src = '/tmp/_sh_src.txt'
with open(src, 'w') as f:
    f.write('line1\nline2\n')
dst = shutil.copy(src, '/tmp/_sh_dst.txt')
print(dst, open(dst).read() == 'line1\nline2\n')
shutil.copyfile(src, '/tmp/_sh_dst2.txt')
print(open('/tmp/_sh_dst2.txt').read().splitlines())
try:
    shutil.copyfile(src, src)
except shutil.SameFileError as e:
    print('SameFileError')
os.remove(src); os.remove(dst); os.remove('/tmp/_sh_dst2.txt')
print(shutil.which('sh') is not None, shutil.which('no_such_cmd_zzz'))
print(glob.has_magic('a*'), glob.has_magic('abc'), glob.escape('a*b?[c]'), glob.glob('/nonexistent_dir/nothing'), glob.glob('/tmp'))
