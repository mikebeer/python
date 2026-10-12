"""Pure-Python subset of matplotlib: pyplot objects that render deterministic SVG text.

Only the SVG output is supported (savefig('x.svg'), show()).  See the manual, section
"matplotlib" in the libraries chapter, for the list of supported calls."""
__version__ = '3.8.0'
__all__ = ['pyplot', 'use', 'rcParams']

rcParams = {
    'figure.figsize': (6.4, 4.8), 'figure.dpi': 100.0, 'font.size': 10.0,
    'lines.linewidth': 1.5, 'lines.markersize': 6.0, 'axes.grid': False,
    'axes.autolimit_mode': 'data', 'axes.xmargin': 0.05, 'axes.ymargin': 0.05,
    'axes.formatter.limits': (-5, 6), 'axes.formatter.useoffset': True,
    'axes.formatter.offset_threshold': 4, 'axes.unicode_minus': True,
    'savefig.format': 'svg', 'backend': 'svg',
}
_backend = ['svg']


def use(backend, force=True):
    _backend[0] = str(backend)


def get_backend():
    return _backend[0]


def is_interactive():
    return False


def interactive(b):
    pass
