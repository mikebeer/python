import tomllib
doc = '''
# comment
title = "TOML \\u00e9xample"
int1 = 42
int2 = -1_000
hex = 0xDEADBEEF
oct = 0o755
bin = 0b1101
f1 = 3.14
f2 = -2e-3
f3 = 6.02_2e23
inf = inf
b1 = true
b2 = false
lit = 'C:\\path\\n'
ml = """
Roses
red \\
   violets"""
mll = \'\'\'
raw\\n
text\'\'\'
d1 = 1979-05-27
t1 = 07:32:00
dt1 = 1979-05-27T07:32:00
dt2 = 1979-05-27T07:32:00Z
dt3 = 1979-05-27 00:32:00.5-07:00
arr = [1, 2, [3, 4], "x"]
multi = [
  1, # one
  2,
]
"quoted key" = 1
dot.b.c = "dotted"
inline = { x = 1, y = { z = "deep" }, "k k" = [1,2] }

[server]
host = "localhost"
ports = [8001, 8002]

[server.limits]
max = 10

[[products]]
name = "Hammer"
sku = 738594937

[[products]]

[[products]]
name = "Nail"
tags = ["a", "b"]

[a.sub]
x = 1
[a]
y = 2
'''
d = tomllib.loads(doc)
def show(o, ind=0):
    if isinstance(o, dict):
        for k in o:
            if isinstance(o[k], (dict, list)):
                print(' ' * ind + repr(k) + ':')
                show(o[k], ind + 2)
            else:
                print(' ' * ind + repr(k), '=', repr(o[k]))
    elif isinstance(o, list):
        for x in o:
            if isinstance(x, (dict, list)):
                print(' ' * ind + '-')
                show(x, ind + 2)
            else:
                print(' ' * ind + '-', repr(x))
show(d)
print(tomllib.loads('x = 1.5', parse_float=lambda s: s), tomllib.loads(''), tomllib.loads('a = []\nb = {}'))
for bad in ['a = ', 'a = 1\na = 2', '[t]\n[t]', 'a = "x', 'a = [1, 2', 'a b = 1', 'a = 01', '[x]\ny = 1\n[x.y]\n', 'a = 1 2', '[[a]]\n[a]\n', 'a = 0x', "a = '''x"]:
    try:
        tomllib.loads(bad)
        print('no error', repr(bad))
    except tomllib.TOMLDecodeError as e:
        print('TOMLDecodeError')
try:
    tomllib.loads(b'x=1')
except TypeError as e:
    print('TypeError')
