import os
from pathlib import Path
t = Path('/tmp/_pl_test.txt')
print(t.write_text('hello\nworld\n'), t.exists(), t.is_file(), t.is_dir(), t.read_text().splitlines(), t.write_bytes(b'\x00\x01'), t.read_bytes())
with t.open('w') as f:
    f.write('via open')
print(t.read_text(), t.name, t.stat().st_size if hasattr(os, 'stat') else 8)
t2 = Path('/tmp/_pl_test2.txt')
t.rename(t2)
print(t.exists(), t2.exists(), t2.read_text())
t2.unlink()
print(t2.exists())
t2.unlink(missing_ok=True)
try:
    t2.unlink()
except FileNotFoundError:
    print('FileNotFoundError')
try:
    t2.read_text()
except FileNotFoundError:
    print('FileNotFoundError read')
print(Path('/tmp').is_dir())
