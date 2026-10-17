import os, tempfile
s = "héllo ✓ ü"
b = s.encode("utf-8")
print(b, len(b))
print(b.decode("utf-8") == s, b.decode("utf-8").encode("utf-8") == b)
print(s)
d = tempfile.mkdtemp()
p = os.path.join(d, "u.txt")
with open(p, "w") as f:
    f.write(s + "\n" + s.upper() + "\n")
with open(p, "rb") as f:
    raw = f.read()
print(raw == (s + "\n" + s.upper() + "\n").encode("utf-8"), len(raw))
with open(p) as f:
    text = f.read()
print(text == s + "\n" + s.upper() + "\n")
with open(p, encoding="utf-8") as f:
    lines = f.readlines()
print(lines == [s + "\n", s.upper() + "\n"])
print([l.rstrip("\n") for l in open(p)] == [s, s.upper()])
with open(p, "wb") as f:
    f.write(b + b"\n")
with open(p) as f:
    print(f.read().strip() == s)
with open(p, "w", encoding="utf-8") as f:
    print(s, file=f)
with open(p, "rb") as f:
    print(f.read() == b + b"\n")
# only the line of the first file read back keeps its characters on output
with open(p) as f:
    print(f.readline(), end="")
os.remove(p)
os.rmdir(d)
