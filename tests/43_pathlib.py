import os
from pathlib import Path, PurePath, PurePosixPath, PosixPath
p = PurePosixPath('/usr/local//lib/./python3.13/site.py')
print(p, repr(p), p.parts, p.name, p.suffix, p.stem, p.parent, p.anchor, p.root, p.drive, p.is_absolute())
print(list(p.parents), p.parents[1], p.parents[-1], len(p.parents))
print(p.with_suffix('.txt'), p.with_name('x.y'), p.with_stem('zz'), PurePath('a.tar.gz').suffixes, PurePath('a.tar.gz').suffix, PurePath('a.tar.gz').stem)
print(PurePath('a') / 'b' / 'c.txt', PurePath('a', 'b', '/c', 'd'), 'x' / PurePath('y'), PurePath('a').joinpath('b', 'c'), PurePath(''), PurePath('.'), PurePath('/'), PurePath('//a'), PurePath('///a'))
print(PurePath('/a/b/c').relative_to('/a'), PurePath('/a/b/c').is_relative_to('/a/b'), PurePath('/a/b/c').is_relative_to('/x'))
try:
    PurePath('/a/b').relative_to('/c')
except ValueError as e:
    print('ValueError', e)
print(PurePath('/a/b').relative_to('/a/c', walk_up=True))
print(PurePath('a/b.py').match('*.py'), PurePath('/a/b/c.py').match('b/*.py'), PurePath('/a/b/c.py').match('/*.py'), PurePath('a/b.py').match('a/*'), PurePath('x.PY').match('*.py'))
print(PurePath('a/b') == PurePath('a//b/'), PurePath('a') < PurePath('b'), sorted([PurePath('b'), PurePath('a/c'), PurePath('a')]), hash(PurePath('a')) == hash(PurePath('./a')), {PurePath('x'): 1}[PurePath('x')])
print(PurePath('/etc/x y').as_posix(), PurePath('/etc/x y').as_uri(), PurePath("a/b").__fspath__(), str(PurePath('a/../b')), PurePath('.hidden').suffix, PurePath('.hidden.txt').stem, PurePath('a.').suffix, PurePath('..').name)
print(type(Path('x')).__name__, type(PurePath('x')).__name__, isinstance(Path('x'), PurePath), isinstance(Path('x'), Path), Path('a') == PurePath('a'), Path('/tmp').is_absolute(), Path('a/b').parent.name)
print(Path('/nonexistent').exists(), Path.cwd().is_absolute(), Path('rel').absolute().is_absolute(), Path('a/../b').resolve().name, Path('~').expanduser() != Path('~'), Path.home().is_absolute())
print(Path('/a/b').joinpath('c'), Path('a') / Path('b'), repr(Path('/a/b')), PurePosixPath('a').parts, Path('/x/y').relative_to('/x'))
try:
    PurePath('a/b').with_suffix('txt')
except ValueError as e:
    print('ValueError', e)
try:
    PurePath('/').with_name('x')
except ValueError as e:
    print('ValueError', e)
