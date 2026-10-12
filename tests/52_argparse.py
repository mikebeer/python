import argparse, sys, io

def run(parser, argv):
    err = io.StringIO()
    old = sys.stderr
    sys.stderr = err
    try:
        ns = parser.parse_args(argv)
        print('OK', sorted(vars(ns).items()))
    except SystemExit as e:
        print('SystemExit', e.args[0] if e.args else None)
        print(err.getvalue().rstrip())
    finally:
        sys.stderr = old

p = argparse.ArgumentParser(prog='tool', description='Process some integers.', epilog='bye')
p.add_argument('integers', metavar='N', type=int, nargs='+', help='an integer for the accumulator')
p.add_argument('--sum', dest='accumulate', action='store_const', const=sum, default=max, help='sum the integers (default: find the max)')
p.add_argument('-v', '--verbose', action='count', default=0, help='verbosity')
p.add_argument('-o', '--output', metavar='FILE', help='output file')
p.add_argument('--mode', choices=['fast', 'slow'], default='fast')
p.add_argument('-t', '--tag', action='append', default=[])
p.add_argument('--no-color', action='store_false', dest='color')
p.add_argument('--ratio', type=float, required=False)
p.add_argument('--version', action='version', version='%(prog)s 1.2')
p.print_help()
print(p.format_usage())
ns = p.parse_args(['1', '2', '3', '-vv', '--sum', '-o', 'out.txt', '--mode=slow', '-t', 'a', '-tb', '--no-color', '--ratio', '0.5'])
print(ns, ns.accumulate(ns.integers))
print(p.parse_args(['4']))
for argv in [[], ['x'], ['1', '--mode', 'medium'], ['1', '--bogus'], ['1', '-o'], ['1', '--ratio', 'abc'], ['1', '--ver'], ['--version'], ['-h'], ['1', '--m', 'slow'], ['1', '-vq']]:
    run(p, argv)
print(p.parse_known_args(['1', '--zzz', '2']))
print(p.parse_intermixed_args(['1', '-v', '2']))

# required options, nargs variants, defaults, type conversion
q = argparse.ArgumentParser(prog='q', add_help=False)
q.add_argument('--req', required=True)
q.add_argument('pos', nargs='?', default='dflt')
q.add_argument('--pair', nargs=2, type=int, metavar=('A', 'B'))
q.add_argument('--opt', nargs='?', const='C', default='D')
q.add_argument('--many', nargs='*', default=None)
q.add_argument('--hex', type=lambda s: int(s, 16))
q.add_argument('--choice', type=int, choices=range(3))
print(q.format_help())
for argv in [['--req', 'r'], ['--req', 'r', 'P', '--pair', '1', '2'], ['--req', 'r', '--opt'], ['--req', 'r', '--opt', 'z', '--many', 'a', 'b', 'c'], ['--req', 'r', '--hex', 'ff'], ['--pair', '1'], ['--req', 'r', '--choice', '5'], ['--req', 'r', '--many'], ['--req=1', '--', '--weird']]:
    run(q, argv)

# subparsers
m = argparse.ArgumentParser(prog='git', description='fake git')
m.add_argument('--debug', action='store_true')
sub = m.add_subparsers(dest='cmd', title='commands', help='sub-command help', metavar='CMD')
a = sub.add_parser('add', help='add files', aliases=['a'])
a.add_argument('paths', nargs='+')
a.add_argument('-f', '--force', action='store_true')
c = sub.add_parser('commit', help='commit changes')
c.add_argument('-m', '--message', required=True)
c.set_defaults(func='do_commit')
m.print_help()
a.print_help()
for argv in [['add', 'x', 'y', '-f'], ['a', 'z'], ['commit', '-m', 'msg'], ['--debug', 'commit', '-m', 'q'], ['commit'], ['nope'], [], ['add']]:
    run(m, argv)

# groups, mutually exclusive
g = argparse.ArgumentParser(prog='g', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
grp = g.add_argument_group('input options', 'how to read')
grp.add_argument('--in', dest='inp', default='stdin', help='input source')
mx = g.add_mutually_exclusive_group(required=True)
mx.add_argument('--a', action='store_true', help='mode a')
mx.add_argument('--b', action='store_true', help='mode b')
g.print_help()
for argv in [['--a'], ['--a', '--b'], [], ['--b', '--in', 'f']]:
    run(g, argv)

# misc: prefix chars, fromfile, set_defaults, namespace, BooleanOptionalAction, custom action, RawTextHelpFormatter
r = argparse.ArgumentParser(prog='r', prefix_chars='-+', fromfile_prefix_chars=None, formatter_class=argparse.RawTextHelpFormatter, allow_abbrev=False, description='line1\n  line2')
r.add_argument('+x', action='store_true', help='plus\nflag')
r.add_argument('--feat', action=argparse.BooleanOptionalAction, default=True)
class Upper(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values.upper())
r.add_argument('--up', action=Upper)
r.add_argument('--ext', action='extend', nargs='+')
r.set_defaults(zzz=1)
r.print_help()
for argv in [['+x', '--no-feat', '--up', 'abc'], ['--ext', 'a', 'b', '--ext', 'c'], ['--fe'], ['--feat', '--no-feat']]:
    run(r, argv)
ns = argparse.Namespace(a=1, b='x')
print(ns, ns.a, 'a' in ns, argparse.Namespace(a=1) == argparse.Namespace(a=1), vars(ns))
try:
    argparse.ArgumentParser(prog='e').add_argument('--x', '--x')
    p2 = argparse.ArgumentParser(prog='e'); p2.add_argument('--x'); p2.add_argument('--x')
except argparse.ArgumentError as e:
    print('ArgumentError', e)
p3 = argparse.ArgumentParser(prog='e', exit_on_error=False)
p3.add_argument('--n', type=int)
try:
    p3.parse_args(['--n', 'q'])
except argparse.ArgumentError as e:
    print('ArgumentError', e)
p4 = argparse.ArgumentParser(prog='very-long-program-name-for-wrapping-test', usage=None)
p4.add_argument('--aaaaaaaaaaaaaaa', help='h' * 100)
p4.add_argument('--bbbbbbbbbbbbbbb', metavar='LONGMETAVAR', nargs='+')
p4.add_argument('--ccccccccccccccc', choices=['one', 'two', 'three'])
p4.add_argument('positional_with_long_name', nargs='*')
p4.print_help()
print(p4.format_usage(), end='')
print(argparse.ArgumentParser(prog='u', usage='%(prog)s [custom]').format_help())
