# _cms native hook (build: cd c && make cms-test): diag58 without a host, then the fake in-memory 3270 host
import _cms
print(_cms.host())
try:
    _cms.diag58(0, bytes([0xF6]))
except NotImplementedError as e:
    print("NotImplementedError")
_cms.fake(True)
print(_cms.host())
# Erase/Write: SBA 0, SF protected, "HELLO", SBA row 2 col 1, SF unprotected, "cmd ", IC
E = {"H": 0xC8, "E": 0xC5, "L": 0xD3, "O": 0xD6}
data = [0xF5, 0xC3, 0x11, 0x40, 0x40, 0x1D, 0x60] + [E[c] for c in "HELLO"]
data += [0x11, 0xC1, 0x50,  # SBA 80 = row 2 col 1
          0x1D, 0x40, 0x83, 0x94, 0x84, 0x40, 0x13]
print(_cms.diag58(0, bytes(data)))
s = _cms.fake_screen().split("\n")
print(len(s), repr(s[0].rstrip()), repr(s[1].rstrip()), _cms.fake_cursor())
_cms.fake_push(0x7D, 2, "ls", 2, 8)
r = _cms.diag58(0, bytes([0xF6]))
print(list(r))
print(list(_cms.diag58(0, bytes([0xF6]))))
try:
    _cms.diag58(0, bytes([0x01]))
except ValueError as e:
    print("ValueError", e)
