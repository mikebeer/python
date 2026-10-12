import configparser, io, sys
text = """
[DEFAULT]
base = /opt
user = bob

[paths]
home = %(base)s/home
log = %(home)s/log
name: value with spaces   
multi = line one
    line two
; a comment
# another
empty =
flag

[server]
port = 8080
ratio = 0.75
debug = yes
timeout=  30
"""
cp = configparser.ConfigParser(allow_no_value=True)
cp.read_string(text)
print(cp.sections(), cp.defaults(), list(cp), len(cp))
print(cp.get('paths', 'log'), cp.get('paths', 'log', raw=True), cp['paths']['home'], cp['paths']['name'], repr(cp['paths']['multi']), repr(cp['paths']['empty']), cp['paths']['flag'])
print(cp.getint('server', 'port'), cp.getfloat('server', 'ratio'), cp.getboolean('server', 'debug'), cp.getint('server', 'timeout'), cp['server'].getint('port'), cp['server'].getboolean('debug'))
print(cp.options('server'), cp.has_section('x'), cp.has_option('server', 'port'), cp.has_option('server', 'base'), 'user' in cp['server'], 'nope' in cp['server'])
print(cp.items('server'), cp.items('paths', raw=True)[:3])
print(cp.get('server', 'missing', fallback='fb'), cp.getint('server', 'missing', fallback=5), cp['server'].get('missing', 'd'), cp['server'].get('missing'), cp.get('nosec', 'x', fallback=None))
for f in [lambda: cp.get('nosec', 'x'), lambda: cp.get('server', 'zzz'), lambda: cp.add_section('server'), lambda: cp.getint('paths', 'name'), lambda: cp['server']['q'], lambda: cp.set('server', 'k', 5), lambda: cp.getboolean('server', 'port')]:
    try:
        f()
    except Exception as e:
        print(type(e).__name__, e)
cp.set('server', 'new', 'val')
cp.add_section('extra')
cp['extra'] = {'a': '1', 'B': '2'}
cp['extra']['c'] = 'three'
cp.remove_option('server', 'ratio')
cp.remove_section('paths')
out = io.StringIO()
cp.write(out)
print(out.getvalue())
cp.write(sys.stdout, space_around_delimiters=False)
r = configparser.RawConfigParser()
r.read_dict({'s': {'x': '%(y)s', 'y': 1}})
print(r.get('s', 'x'), r.get('s', 'y'), r.sections())
e = configparser.ConfigParser(interpolation=configparser.ExtendedInterpolation())
e.read_string("[a]\nx = 1\n[b]\ny = ${a:x}-${z}\nz = q\n")
print(e.get('b', 'y'))
bad = configparser.ConfigParser()
for t in ["k = v\n", "[a]\nk = v\nk = w\n", "[a]\n[a]\n", "[a]\nnovalue\n", "[a]\nx = %(nope)s\n"]:
    try:
        bad.read_string(t)
        if bad.has_section('a'):
            print(bad.get('a', 'x'))
    except configparser.Error as ex:
        print(type(ex).__name__, str(ex).replace('\n', '|'))
    bad = configparser.ConfigParser()
c2 = configparser.ConfigParser(inline_comment_prefixes=('#',), delimiters=('=',))
c2.read_string("[s]\na = 1 # c\nb = x#y\nc: d = e\n")
print(dict(c2['s']), c2['s'].name, c2['s'].parser is c2, repr(c2['s']))
c3 = configparser.ConfigParser(defaults={'k': 'v'}, strict=False)
c3.read_string("[s]\nx=1\nx=2\n")
print(c3.get('s', 'x'), c3.get('s', 'k'), c3.optionxform('AbC'))
c3.optionxform = str
c3.read_string("[t]\nCamelCase = 1\n")
print(c3.options('t'))
