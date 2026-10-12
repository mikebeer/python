import fnmatch, shlex, html, uuid
names = ['a.py', 'b.txt', 'test_x.py', 'Test.PY', 'x[1].c', 'data_01.csv', 'data_1.csv', '.hidden']
for pat in ['*.py', 'test_*', '?.txt', '[ab].*', 'data_[0-9]*', 'data_[!0]*', '*[1]*', '*', '[]x]*', 'x[[]1].c']:
    print(pat, fnmatch.filter(names, pat), fnmatch.fnmatchcase('Test.PY', pat), fnmatch.translate(pat))
print(shlex.split('a "b c" d\\ e \'f g\' h"i j"k # comment'), shlex.split('x # c', comments=True), shlex.split("a 'b'\"c\"d"))
print(shlex.quote("it's"), shlex.quote('safe-1.2/x'), shlex.quote(''), shlex.quote('a b$c'), shlex.join(['ls', 'my file', "o'k"]))
print(list(shlex.shlex('a=b; c  "d e"', posix=True)), list(shlex.shlex('x = "q r" y')))
lex = shlex.shlex('ls -l | grep "a b" > out', punctuation_chars=True)
print(list(lex))
for bad in ['"unclosed', 'trailing\\']:
    try:
        shlex.split(bad)
    except ValueError as e:
        print('ValueError', e)
print(html.escape('<a href="x">T&C\'s</a>'), html.escape('"q"', quote=False))
print(html.unescape('&lt;b&gt; &amp;amp; &quot;q&quot; &#65;&#x42; &copy; &nbsp;| &unknown; &amp &#128512; &#0;'))
u = uuid.UUID('12345678-1234-5678-1234-567812345678')
print(u, repr(u), u.hex, u.int, u.urn, u.version, u.variant, u.fields, u.time_low, u.node, u.bytes, u.bytes_le)
print(uuid.UUID('{12345678123456781234567812345678}') == u, uuid.UUID(bytes=u.bytes) == u, uuid.UUID(int=u.int) == u, uuid.UUID(fields=u.fields) == u, uuid.UUID(bytes_le=u.bytes_le) == u, hash(u) == hash(uuid.UUID(int=u.int)))
print(uuid.uuid3(uuid.NAMESPACE_DNS, 'python.org'), uuid.uuid5(uuid.NAMESPACE_DNS, 'python.org'), uuid.uuid3(uuid.NAMESPACE_URL, 'http://x'), uuid.uuid5(uuid.NAMESPACE_OID, 'o'), uuid.uuid5(uuid.NAMESPACE_X500, 'x'))
import random
random.seed(1)
x = uuid.uuid4()
print(x.version, x.variant, len(str(x)), str(x)[14], str(x)[19] in '89ab', uuid.uuid4() != x, uuid.uuid1().version)
print(uuid.UUID(int=0) < uuid.UUID(int=1), sorted([uuid.UUID(int=3), uuid.UUID(int=2)]))
for bad in ['xyz', '1234']:
    try:
        uuid.UUID(bad)
    except ValueError as e:
        print('ValueError', e)
try:
    u.int = 5
except TypeError as e:
    print('TypeError', e)
