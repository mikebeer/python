"""matplotlib.pyplot subset: Figure / Axes objects that render SVG text.  Deterministic, pure Python."""
import math
import matplotlib
from matplotlib import colors as _colors
from matplotlib import ticker as _ticker

try:
    import numpy as _np
except Exception:
    _np = None

PX = 100.0 / 72.0           # pixels per point at the default 100 dpi (set per figure)
_ISDIG = '0123456789'
_CW = {'.': .318, ',': .318, ' ': .318, '-': .838, '−': .838, '+': .838, 'e': .615, '%': .95,
       '(': .39, ')': .39, 'i': .278, 'l': .278, 'j': .278, 't': .392, 'f': .352, 'r': .411,
       'm': .974, 'w': .818, 'W': .989, 'M': .863, 'I': .295, ':': .337, '/': .337}


def _tw(s, fs):
    """Estimated width in points of text s (DejaVu Sans metrics, approximate)."""
    w = 0.0
    for ch in s.replace('\u2212', '-'):
        if ch in _ISDIG:
            w += .636
        elif ch in _CW:
            w += _CW[ch]
        elif ch.isupper():
            w += .68
        else:
            w += .6
    return w * fs


def _n(v):
    s = '%.3f' % v
    if '.' in s:
        s = s.rstrip('0').rstrip('.')
    if s == '-0':
        s = '0'
    return s


def _esc(s):
    return str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def _arr(x):
    if hasattr(x, 'tolist'):
        x = x.tolist()
    if isinstance(x, (int, float)):
        return [x]
    return list(x)


def _out(lst):
    if _np is not None:
        try:
            return _np.array(lst)
        except Exception:
            return lst
    return lst


def _fin(v):
    return isinstance(v, (int, float)) and math.isfinite(v)


class _Cycle:
    def __init__(self):
        self.i = 0

    def next(self):
        c = _colors.TAB10[self.i % 10]
        self.i += 1
        return c


class Artist:
    kind = 0
    label = ''
    sticky_x = ()
    sticky_y = ()

    def get_label(self):
        return self.label

    def set_label(self, s):
        self.label = s

    def _extent(self):
        return None


def _ls_name(ls):
    return {'solid': '-', 'dashed': '--', 'dotted': ':', 'dashdot': '-.'}.get(ls, ls)


class Line2D(Artist):
    kind = 0

    def __init__(self, x, y, color='C0', linestyle='-', linewidth=1.5, marker='None', markersize=6.0,
                 label='', alpha=None, markerfacecolor=None, drawstyle='default', kind=0, transform=None):
        self.x = [float(v) for v in x]
        self.y = [float(v) for v in y]
        self._raw = color
        self.color = _colors.to_hex(color)
        self.linestyle = _ls_name(linestyle)
        self.linewidth = linewidth
        self.marker = marker if marker is not None else 'None'
        self.markersize = markersize
        self.label = label
        self.alpha = alpha
        self.markerfacecolor = markerfacecolor
        self.transform = transform
        self.kind = kind

    def get_xdata(self, orig=True):
        return _out(self.x)

    def get_ydata(self, orig=True):
        return _out(self.y)

    def get_data(self, orig=True):
        return (_out(self.x), _out(self.y))

    def set_data(self, x, y):
        self.x = [float(v) for v in _arr(x)]
        self.y = [float(v) for v in _arr(y)]

    def set_xdata(self, x):
        self.x = [float(v) for v in _arr(x)]

    def set_ydata(self, y):
        self.y = [float(v) for v in _arr(y)]

    def get_color(self):
        return self._raw

    def set_color(self, c):
        self._raw = c
        self.color = _colors.to_hex(c)

    def get_linestyle(self):
        return self.linestyle

    def set_linestyle(self, s):
        self.linestyle = _ls_name(s)

    def get_linewidth(self):
        return self.linewidth

    def set_linewidth(self, w):
        self.linewidth = w

    def get_marker(self):
        return self.marker

    def set_marker(self, m):
        self.marker = m

    def get_markersize(self):
        return self.markersize

    def get_alpha(self):
        return self.alpha

    def _extent(self):
        if self.transform == 'xaxis':
            return (None, None, min(self.y), max(self.y))
        if self.transform == 'yaxis':
            return (min(self.x), max(self.x), None, None)
        xs = [v for v in self.x if _fin(v)]
        ys = [v for v in self.y if _fin(v)]
        if not xs or not ys:
            return None
        return (min(xs), max(xs), min(ys), max(ys))


class Rectangle(Artist):
    kind = 1

    def __init__(self, x, y, w, h, color='C0', edgecolor='none', alpha=None, label='', linewidth=1.0):
        self.x, self.y, self.w, self.h = float(x), float(y), float(w), float(h)
        self.color = _colors.to_hex(color)
        self.edgecolor = _colors.to_hex(edgecolor)
        self.alpha = alpha
        self.label = label
        self.linewidth = linewidth

    def get_x(self):
        return self.x

    def get_y(self):
        return self.y

    def get_width(self):
        return self.w

    def get_height(self):
        return self.h

    def get_xy(self):
        return (self.x, self.y)

    def get_facecolor(self):
        return self.color

    def get_label(self):
        return self.label

    def _extent(self):
        return (min(self.x, self.x + self.w), max(self.x, self.x + self.w),
                min(self.y, self.y + self.h), max(self.y, self.y + self.h))


class Polygon(Artist):
    kind = 1

    def __init__(self, pts, color='C0', alpha=None, label='', edgecolor='none', linewidth=1.0):
        self.pts = [(float(a), float(b)) for a, b in pts]
        self.color = _colors.to_hex(color)
        self.alpha = alpha
        self.label = label
        self.edgecolor = _colors.to_hex(edgecolor)
        self.linewidth = linewidth

    def _extent(self):
        xs = [p[0] for p in self.pts if _fin(p[0])]
        ys = [p[1] for p in self.pts if _fin(p[1])]
        if not xs or not ys:
            return None
        return (min(xs), max(xs), min(ys), max(ys))


class Wedge(Polygon):
    def __init__(self, center, r, theta1, theta2, color='C0', label=''):
        self.center, self.r, self.theta1, self.theta2 = center, r, theta1, theta2
        n = max(2, int(abs(theta2 - theta1) / 3.0) + 1)
        pts = [center]
        for i in range(n + 1):
            a = math.radians(theta1 + (theta2 - theta1) * i / n)
            pts.append((center[0] + r * math.cos(a), center[1] + r * math.sin(a)))
        Polygon.__init__(self, pts, color=color, label=label, edgecolor='#ffffff', linewidth=1.0)


class PathCollection(Artist):
    kind = 2

    def __init__(self, x, y, s, colors, alpha=None, marker='o', label='', edgecolor=None):
        self.x = [float(v) for v in x]
        self.y = [float(v) for v in y]
        self.s = s
        self.colors = colors
        self.alpha = alpha
        self.marker = marker
        self.label = label
        self.edgecolor = edgecolor

    def get_offsets(self):
        return _out([[a, b] for a, b in zip(self.x, self.y)])

    def get_facecolor(self):
        return self.colors[0]

    def _extent(self):
        xs = [v for v in self.x if _fin(v)]
        ys = [v for v in self.y if _fin(v)]
        if not xs or not ys:
            return None
        return (min(xs), max(xs), min(ys), max(ys))


class Text(Artist):
    def __init__(self, x, y, s, fontsize=10.0, color='black', ha='left', va='baseline', rotation=0,
                 transform='data', weight='normal'):
        self.x, self.y, self.s = x, y, str(s)
        self.fontsize = fontsize
        self.color = _colors.to_hex(color)
        self.ha, self.va = ha, va
        self.rotation = rotation
        self.transform = transform
        self.weight = weight

    def get_text(self):
        return self.s

    def set_text(self, s):
        self.s = str(s)

    def get_position(self):
        return (self.x, self.y)

    def get_fontsize(self):
        return self.fontsize

    def get_rotation(self):
        return self.rotation

    def get_color(self):
        return self.color


class BarContainer:
    kind = 1

    def __init__(self, patches, label=''):
        self.patches = list(patches)
        self.label = label

    def __len__(self):
        return len(self.patches)

    def __iter__(self):
        return iter(self.patches)

    def __getitem__(self, i):
        return self.patches[i]

    def get_label(self):
        return self.label


class _AxArray:
    """Result of subplots(): indexable like a numpy object array."""

    def __init__(self, items, shape):
        self._items = list(items)
        self.shape = shape

    def __len__(self):
        return self.shape[0]

    def __iter__(self):
        return iter([self[i] for i in _range(self.shape[0])])

    def __getitem__(self, k):
        if isinstance(k, tuple):
            r, c = k
            if len(self.shape) == 1:
                return self._items[c if self.shape[0] == 1 and False else r]
            return self._items[r * self.shape[1] + c]
        if len(self.shape) == 2 and isinstance(k, int):
            c = self.shape[1]
            return _AxArray([self._items[k * c + j] for j in _range(c)], (c,))
        return self._items[k]

    @property
    def flat(self):
        return iter(list(self._items))

    def flatten(self):
        return _AxArray(self._items, (len(self._items),))

    ravel = flatten

    def tolist(self):
        return list(self._items)

    @property
    def size(self):
        return len(self._items)


class _Axis:
    """Per-axis state shared with locator / formatter (view interval and tick space)."""

    def __init__(self, ax, name):
        self.ax = ax
        self.name = name
        self.locator = _ticker.AutoLocator()
        self.locator.set_axis(self)
        self.formatter = _ticker.ScalarFormatter()
        self.formatter.set_axis(self)
        self.fixed_ticks = None
        self.fixed_labels = None
        self.cat = None
        self.label = ''
        self.lim = None
        self.inverted = False
        self.label_kw = {}
        self.tick_kw = {}

    def get_view_interval(self):
        return self.ax._view(self.name)

    def get_major_formatter(self):
        return self.formatter

    def get_major_locator(self):
        return self.locator

    def get_offset_text(self):
        return Text(0, 0, self.ax._ticks(self.name)[3])

    def get_label(self):
        return Text(0, 0, self.label)

    def get_ticklocs(self):
        return _out(self.ax._ticks(self.name)[0])

    def get_tick_space(self):
        w, h = self.ax._size_in()
        if self.name == 'x':
            return int(math.floor(w * 72 / (self.ax._tick_fs * 3)))
        return int(math.floor(h * 72 / (self.ax._tick_fs * 2)))


_LOCS = {'best': 0, 'upper right': 1, 'upper left': 2, 'lower left': 3, 'lower right': 4, 'right': 5,
         'center left': 6, 'center right': 7, 'lower center': 8, 'upper center': 9, 'center': 10}


class Axes:
    def __init__(self, fig, pos, number):
        self.figure = fig
        self.number = number
        self._pos = list(pos)
        self.lines = []
        self.patches = []
        self.collections = []
        self.texts = []
        self.containers = []
        self._items = []             # all drawable artists in creation order
        self._cycle = _Cycle()
        self._axes = {'x': _Axis(self, 'x'), 'y': _Axis(self, 'y')}
        self._title = ''
        self._title_kw = {}
        self._grid = {'x': False, 'y': False}
        self._grid_kw = {}
        self._legend = None
        self._tick_fs = 10.0
        self._aspect = None
        self._axis_off = False
        self._cache = {}
        self.transAxes = 'axes'
        self.transData = 'data'
        self.name = 'rectilinear'
        self.xaxis = self._axes['x']
        self.yaxis = self._axes['y']

    # ----- geometry
    def get_position(self):
        return tuple(self._pos)

    def set_position(self, pos):
        self._pos = list(pos)

    def _size_in(self):
        return (self._pos[2] * self.figure.figsize[0], self._pos[3] * self.figure.figsize[1])

    def _box(self):
        W, H = self.figure._wh()
        x0, x1 = self._pos[0] * W, (self._pos[0] + self._pos[2]) * W
        yb, yt = (1 - self._pos[1]) * H, (1 - self._pos[1] - self._pos[3]) * H
        if self._aspect is not None:
            xl, xr = self._view('x')
            yl, yu = self._view('y')
            ratio = abs((yu - yl) / (xr - xl)) * self._aspect
            w, h = x1 - x0, yb - yt
            if h / w > ratio:
                nh = w * ratio
                c = (yb + yt) / 2
                yt, yb = c - nh / 2, c + nh / 2
            else:
                nw = h / ratio
                c = (x0 + x1) / 2
                x0, x1 = c - nw / 2, c + nw / 2
        return x0, x1, yt, yb

    # ----- artists
    def _add(self, art):
        self._items.append(art)
        if isinstance(art, Line2D):
            self.lines.append(art)
        elif isinstance(art, PathCollection):
            self.collections.append(art)
        elif isinstance(art, (Rectangle, Polygon)):
            self.patches.append(art)
        elif isinstance(art, Text):
            self.texts.append(art)
        self._cache = {}
        return art

    def get_lines(self):
        return list(self.lines)

    def get_children(self):
        return list(self._items)

    def _color(self, kw, key='color'):
        c = kw.get(key)
        if c is None:
            return self._cycle.next()
        return c

    def _xy(self, args):
        x = y = None
        if len(args) == 1:
            y = _arr(args[0])
            x = list(range(len(y)))
        else:
            x, y = _arr(args[0]), _arr(args[1])
        return x, y

    def plot(self, *args, **kw):
        res = []
        args = list(args)
        i = 0
        while i < len(args):
            x = y = None
            if i + 1 < len(args) and not isinstance(args[i + 1], str):
                x, y = args[i], args[i + 1]
                i += 2
            else:
                y = args[i]
                i += 1
            fmt = None
            if i < len(args) and isinstance(args[i], str):
                fmt = args[i]
                i += 1
            yl = _arr(y)
            cols = None
            if len(yl) and isinstance(yl[0], (list, tuple)):
                n = len(yl[0])
                cols = [[row[j] for row in yl] for j in range(n)]
            else:
                cols = [yl]
            for col in cols:
                xl = list(range(len(col))) if x is None else self._cat_x(_arr(x))
                if len(xl) != len(col):
                    raise ValueError('x and y must have same first dimension, but have shapes (%d,) and (%d,)'
                                     % (len(xl), len(col)))
                res.append(self._line(xl, col, fmt, kw))
        return res

    def _cat_x(self, xs):
        if xs and isinstance(xs[0], str):
            ax = self._axes['x']
            if ax.cat is None:
                ax.cat = []
            out = []
            for v in xs:
                if v not in ax.cat:
                    ax.cat.append(v)
                out.append(float(ax.cat.index(v)))
            return out
        return xs

    def _line(self, xl, yl, fmt, kw):
        color = kw.get('color', kw.get('c'))
        ls = kw.get('linestyle', kw.get('ls'))
        marker = kw.get('marker')
        if fmt:
            color2, marker2, ls2 = _parse_fmt(fmt)
            color = color if color is not None else color2
            marker = marker if marker is not None else marker2
            ls = ls if ls is not None else ls2
        if color is None:
            color = self._cycle.next()
        if ls is None:
            ls = 'None' if (marker not in (None, 'None') and fmt and not _has_ls(fmt)) else '-'
        ln = Line2D(xl, [float('nan') if v is None else v for v in yl], color=color, linestyle=ls,
                    linewidth=kw.get('linewidth', kw.get('lw', 1.5)),
                    marker=marker if marker is not None else 'None',
                    markersize=kw.get('markersize', kw.get('ms', 6.0)),
                    label=kw.get('label', ''), alpha=kw.get('alpha'))
        self._add(ln)
        return ln

    def scatter(self, x, y, s=None, c=None, marker='o', cmap=None, alpha=None, label='', color=None,
                edgecolors=None, vmin=None, vmax=None, linewidths=None, **kw):
        xl, yl = self._cat_x(_arr(x)), _arr(y)
        n = len(xl)
        cols = None
        if color is not None:
            c = color
        if c is None:
            cols = [self._cycle.next()] * n
        elif isinstance(c, str):
            cols = [_colors.to_hex(c)] * n
        else:
            cl = _arr(c)
            if len(cl) and isinstance(cl[0], (int, float)) and not (len(cl) in (3, 4) and n != len(cl)):
                lo = min(cl) if vmin is None else vmin
                hi = max(cl) if vmax is None else vmax
                span = (hi - lo) or 1.0
                cols = [_colors.cmap_hex((v - lo) / span) for v in cl]
            elif len(cl) and isinstance(cl[0], (int, float)):
                cols = [_colors.to_hex(cl)] * n
            else:
                cols = [_colors.to_hex(v) for v in cl]
        sz = [36.0] * n if s is None else ([float(s)] * n if isinstance(s, (int, float)) else
                                           [float(v) for v in _arr(s)])
        pc = PathCollection(xl, yl, sz, cols, alpha=alpha, marker=marker, label=label)
        return self._add(pc)

    def bar(self, x, height, width=0.8, bottom=None, align='center', color=None, edgecolor=None,
            label='', alpha=None, yerr=None, tick_label=None, **kw):
        return self._bars(x, height, width, bottom, align, color, edgecolor, label, alpha, False, tick_label)

    def barh(self, y, width, height=0.8, left=None, align='center', color=None, edgecolor=None,
             label='', alpha=None, tick_label=None, **kw):
        return self._bars(y, width, height, left, align, color, edgecolor, label, alpha, True, tick_label)

    def _bars(self, pos, val, wid, base, align, color, edgecolor, label, alpha, horiz, tick_label):
        pl = _arr(pos)
        if pl and isinstance(pl[0], str):
            ax = self._axes['y' if horiz else 'x']
            ax.cat = list(pl)
            pl = [float(i) for i in range(len(pl))]
        vl = _arr(val)
        n = len(vl)
        wl = [float(wid)] * n if isinstance(wid, (int, float)) else [float(v) for v in _arr(wid)]
        bl = [0.0] * n if base is None else ([float(base)] * n if isinstance(base, (int, float))
                                              else [float(v) for v in _arr(base)])
        if color is None:
            colr = [self._cycle.next()] * n
        elif isinstance(color, str):
            colr = [color] * n
        else:
            colr = list(color)
        rects = []
        for i in range(n):
            p = float(pl[i])
            if align == 'center':
                p0 = p - wl[i] / 2.0
            else:
                p0 = p
            col = colr[i % len(colr)]
            if horiz:
                r = Rectangle(bl[i], p0, vl[i], wl[i], color=col, edgecolor=edgecolor or 'none', alpha=alpha)
                r.sticky_x = (bl[i],)
            else:
                r = Rectangle(p0, bl[i], wl[i], vl[i], color=col, edgecolor=edgecolor or 'none', alpha=alpha)
                r.sticky_y = (bl[i],)
            rects.append(r)
            self._add(r)
        if tick_label is not None:
            self._axes['y' if horiz else 'x'].fixed_ticks = [float(v) for v in pl]
            self._axes['y' if horiz else 'x'].fixed_labels = [str(v) for v in tick_label]
        bc = BarContainer(rects, label)
        self.containers.append(bc)
        for r in rects:
            r.label = ''
        if rects:
            rects[0].label = label
        self._cache = {}
        return bc

    def hist(self, x, bins=10, range=None, density=False, color=None, alpha=None, label='',
             edgecolor=None, histtype='bar', weights=None, **kw):
        xs = [float(v) for v in _arr(x) if _fin(float(v))]
        if isinstance(bins, int):
            lo, hi = (min(xs), max(xs)) if range is None else range
            if lo == hi:
                lo -= 0.5
                hi += 0.5
            edges = [lo + (hi - lo) * i / bins for i in _range(bins + 1)]
        else:
            edges = [float(v) for v in _arr(bins)]
        nb = len(edges) - 1
        counts = [0.0] * nb
        for k, v in enumerate(xs):
            wv = 1.0 if weights is None else float(_arr(weights)[k])
            if v < edges[0] or v > edges[-1]:
                continue
            j = nb - 1
            for t in _range(nb):
                if v < edges[t + 1]:
                    j = t
                    break
            counts[j] += wv
        if density:
            tot = sum(counts)
            counts = [counts[t] / (tot * (edges[t + 1] - edges[t])) for t in _range(nb)]
        col = color if color is not None else self._cycle.next()
        rects = []
        for t in _range(nb):
            r = Rectangle(edges[t], 0.0, edges[t + 1] - edges[t], counts[t], color=col,
                          edgecolor=edgecolor or 'none', alpha=alpha)
            r.sticky_y = (0.0,)
            rects.append(r)
            self._add(r)
        bc = BarContainer(rects, label)
        self.containers.append(bc)
        if rects:
            rects[0].label = label
        return (_out(counts), _out(edges), bc)

    def pie(self, x, labels=None, colors=None, autopct=None, startangle=0, counterclock=True,
            explode=None, radius=1, **kw):
        v = [float(a) for a in _arr(x)]
        tot = sum(v)
        ang = float(startangle)
        wedges, texts, autos = [], [], []
        for i in _range(len(v)):
            frac = v[i] / tot
            d = 360.0 * frac
            t1, t2 = (ang, ang + d) if counterclock else (ang - d, ang)
            col = colors[i % len(colors)] if colors else self._cycle.next()
            w = Wedge((0.0, 0.0), radius, t1, t2, color=col,
                      label=str(labels[i]) if labels is not None else '')
            self._add(w)
            wedges.append(w)
            mid = math.radians((t1 + t2) / 2.0)
            if labels is not None:
                texts.append(self._add(Text(1.1 * radius * math.cos(mid), 1.1 * radius * math.sin(mid),
                                            labels[i], ha='left' if math.cos(mid) >= 0 else 'right',
                                            va='center')))
            if autopct:
                s = autopct % (100.0 * frac) if isinstance(autopct, str) else autopct(100.0 * frac)
                autos.append(self._add(Text(0.6 * radius * math.cos(mid), 0.6 * radius * math.sin(mid),
                                            s, ha='center', va='center')))
            ang = t2 if counterclock else t1
        self._axes['x'].lim = (-1.25, 1.25)
        self._axes['y'].lim = (-1.25, 1.25)
        self._aspect = 1.0
        self._axis_off = True
        if autopct:
            return wedges, texts, autos
        return wedges, texts

    def errorbar(self, x, y, yerr=None, xerr=None, fmt='', capsize=None, label='', color=None, **kw):
        xl, yl = _arr(x), _arr(y)
        n = len(xl)
        col = color if color is not None else self._cycle.next()
        ln = Line2D(xl, yl, color=col, linestyle='None' if fmt else '-', linewidth=kw.get('linewidth', 1.5),
                    marker=fmt.replace('-', '') if fmt and fmt.replace('-', '') else 'None', label=label)
        if fmt and '-' in fmt:
            ln.linestyle = '-'
        self._add(ln)
        bars = []
        for err, vert in ((yerr, True), (xerr, False)):
            if err is None:
                continue
            if isinstance(err, (int, float)):
                lo = hi = [float(err)] * n
            else:
                el = _arr(err)
                if len(el) == 2 and isinstance(el[0], (list, tuple)):
                    lo, hi = el[0], el[1]
                else:
                    lo = hi = [float(e) for e in el]
            for i in _range(n):
                if vert:
                    seg = Line2D([xl[i], xl[i]], [yl[i] - lo[i], yl[i] + hi[i]], color=col, linewidth=1.5)
                else:
                    seg = Line2D([xl[i] - lo[i], xl[i] + hi[i]], [yl[i], yl[i]], color=col, linewidth=1.5)
                self._add(seg)
                bars.append(seg)
                if capsize:
                    cs = capsize / 2.0 * PX / self.figure.dpi * 100.0
                    for e in ((yl[i] - lo[i], yl[i] + hi[i]) if vert else (xl[i] - lo[i], xl[i] + hi[i])):
                        cp = Line2D([xl[i], xl[i]] if vert else [e, e], [e, e] if vert else [yl[i], yl[i]],
                                    color=col, linewidth=1.5, marker='_' if vert else '|',
                                    markersize=capsize * 2.0, linestyle='None')
                        self._add(cp)
                        bars.append(cp)
        for b in bars:
            b.label = ''
        return (ln, bars)

    def fill_between(self, x, y1, y2=0, color=None, alpha=None, label='', **kw):
        xl = _arr(x)
        a = [float(v) for v in _arr(y1)] if not isinstance(y1, (int, float)) else [float(y1)] * len(xl)
        b = [float(v) for v in _arr(y2)] if not isinstance(y2, (int, float)) else [float(y2)] * len(xl)
        col = color if color is not None else kw.get('facecolor') or self._cycle.next()
        pts = [(xl[i], a[i]) for i in _range(len(xl))] + [(xl[i], b[i]) for i in _range(len(xl) - 1, -1, -1)]
        return self._add(Polygon(pts, color=col, alpha=alpha, label=label))

    def axhline(self, y=0, xmin=0, xmax=1, **kw):
        ln = Line2D([xmin, xmax], [y, y], color=kw.get('color', 'C0'), linestyle=kw.get('linestyle', kw.get('ls', '-')),
                    linewidth=kw.get('linewidth', kw.get('lw', 1.5)), label=kw.get('label', ''),
                    alpha=kw.get('alpha'), transform='xaxis')
        if 'color' not in kw:
            ln.color = self._cycle.next()
        return self._add(ln)

    def axvline(self, x=0, ymin=0, ymax=1, **kw):
        ln = Line2D([x, x], [ymin, ymax], color=kw.get('color', 'C0'), linestyle=kw.get('linestyle', kw.get('ls', '-')),
                    linewidth=kw.get('linewidth', kw.get('lw', 1.5)), label=kw.get('label', ''),
                    alpha=kw.get('alpha'), transform='yaxis')
        if 'color' not in kw:
            ln.color = self._cycle.next()
        return self._add(ln)

    def text(self, x, y, s, fontsize=10.0, color='black', ha='left', va='baseline', rotation=0,
             transform=None, fontweight='normal', **kw):
        t = Text(x, y, s, fontsize=_fsize(fontsize), color=color, ha=kw.get('horizontalalignment', ha),
                 va=kw.get('verticalalignment', va), rotation=rotation,
                 transform='axes' if transform == 'axes' else 'data', weight=fontweight)
        return self._add(t)

    def annotate(self, text, xy, xytext=None, arrowprops=None, fontsize=10.0, color='black', ha='left',
                 va='baseline', **kw):
        pos = xytext if xytext is not None else xy
        t = Text(pos[0], pos[1], text, fontsize=_fsize(fontsize), color=color, ha=ha, va=va)
        t.xy = xy
        t.arrow = arrowprops
        return self._add(t)

    # ----- decoration
    def set_title(self, s, fontsize=None, **kw):
        self._title = str(s)
        self._title_kw = {'fontsize': _fsize(fontsize) if fontsize else 12.0}
        return self._title

    def get_title(self, loc='center'):
        return self._title

    def set_xlabel(self, s, fontsize=None, **kw):
        self._axes['x'].label = str(s)
        self._axes['x'].label_kw = {'fontsize': _fsize(fontsize) if fontsize else 10.0}

    def set_ylabel(self, s, fontsize=None, **kw):
        self._axes['y'].label = str(s)
        self._axes['y'].label_kw = {'fontsize': _fsize(fontsize) if fontsize else 10.0}

    def get_xlabel(self):
        return self._axes['x'].label

    def get_ylabel(self):
        return self._axes['y'].label

    def set(self, **kw):
        for k, v in kw.items():
            getattr(self, 'set_' + k)(v)

    def grid(self, visible=None, which='major', axis='both', **kw):
        if visible is None:
            visible = not (self._grid['x'] and self._grid['y'])
            if kw:
                visible = True
        for a in ('x', 'y'):
            if axis in ('both', a):
                self._grid[a] = bool(visible)
        self._grid_kw = kw

    def set_xlim(self, left=None, right=None, **kw):
        if isinstance(left, (tuple, list)):
            left, right = left
        cur = self._view('x')
        self._axes['x'].lim = (cur[0] if left is None else float(left), cur[1] if right is None else float(right))
        self._cache = {}
        return self.get_xlim()

    def set_ylim(self, bottom=None, top=None, **kw):
        if isinstance(bottom, (tuple, list)):
            bottom, top = bottom
        cur = self._view('y')
        self._axes['y'].lim = (cur[0] if bottom is None else float(bottom), cur[1] if top is None else float(top))
        self._cache = {}
        return self.get_ylim()

    def get_xlim(self):
        v = self._view('x')
        return (v[1], v[0]) if self._axes['x'].inverted else v

    def get_ylim(self):
        v = self._view('y')
        return (v[1], v[0]) if self._axes['y'].inverted else v

    def invert_xaxis(self):
        self._axes['x'].inverted = not self._axes['x'].inverted

    def invert_yaxis(self):
        self._axes['y'].inverted = not self._axes['y'].inverted

    def autoscale(self, enable=True, axis='both', tight=None):
        if enable:
            for a in ('x', 'y'):
                if axis in ('both', a):
                    self._axes[a].lim = None
            self._cache = {}

    def margins(self, x=None, y=None):
        pass

    def set_aspect(self, aspect, adjustable=None, **kw):
        self._aspect = 1.0 if aspect == 'equal' else (None if aspect == 'auto' else float(aspect))

    def axis(self, arg=None, *a, **kw):
        if arg == 'off':
            self._axis_off = True
        elif arg == 'on':
            self._axis_off = False
        elif arg in ('equal', 'scaled', 'square'):
            self._aspect = 1.0
        elif arg == 'tight':
            pass
        elif isinstance(arg, (list, tuple)):
            self.set_xlim(arg[0], arg[1])
            self.set_ylim(arg[2], arg[3])
        return self.get_xlim() + self.get_ylim()

    def set_xticks(self, ticks, labels=None, **kw):
        ax = self._axes['x']
        ax.fixed_ticks = [float(v) for v in _arr(ticks)]
        ax.fixed_labels = None if labels is None else [str(v) for v in _arr(labels)]
        ax.tick_kw = kw
        self._cache = {}

    def set_yticks(self, ticks, labels=None, **kw):
        ax = self._axes['y']
        ax.fixed_ticks = [float(v) for v in _arr(ticks)]
        ax.fixed_labels = None if labels is None else [str(v) for v in _arr(labels)]
        ax.tick_kw = kw
        self._cache = {}

    def set_xticklabels(self, labels, **kw):
        self._axes['x'].fixed_labels = [str(v) for v in _arr(labels)]
        self._axes['x'].tick_kw = kw

    def set_yticklabels(self, labels, **kw):
        self._axes['y'].fixed_labels = [str(v) for v in _arr(labels)]
        self._axes['y'].tick_kw = kw

    def tick_params(self, axis='both', **kw):
        for a in ('x', 'y'):
            if axis in ('both', a):
                self._axes[a].tick_kw.update(kw)
        if 'labelsize' in kw:
            self._tick_fs = _fsize(kw['labelsize'])

    def get_xticks(self):
        return _out(self._ticks('x')[0])

    def get_yticks(self):
        return _out(self._ticks('y')[0])

    def get_xticklabels(self):
        return [Text(p, 0, s) for p, s in zip(*self._all_labels('x'))]

    def get_yticklabels(self):
        return [Text(0, p, s) for p, s in zip(*self._all_labels('y'))]

    def legend(self, *args, loc='best', title=None, frameon=True, fontsize=10.0, ncol=1, **kw):
        handles = None
        labels = None
        if len(args) == 1:
            labels = [str(v) for v in args[0]]
        elif len(args) == 2:
            handles, labels = args[0], [str(v) for v in args[1]]
        if handles is None:
            hs = self._legend_handles()
            if labels is not None:
                hs = hs[:len(labels)]
                self._legend = [(h, l) for h, l in zip(hs, labels)]
            else:
                self._legend = hs and [(h, h.label if not isinstance(h, BarContainer) else h.label) for h in hs]
        else:
            self._legend = list(zip(handles, labels))
        self._legend = [(h, l) for h, l in (self._legend or []) if l and not str(l).startswith('_')]
        self._legend_obj = self
        self._legend_kw = {'loc': loc, 'title': title, 'frameon': frameon, 'fontsize': _fsize(fontsize)}
        return self._legend

    def get_legend(self):
        return self._legend_obj if self._legend else None

    def _legend_handles(self):
        hs = [a for a in self.lines if a.label and not a.label.startswith('_')]
        for c in self.containers:
            if c.label and not str(c.label).startswith('_'):
                hs.append(c)
        for a in self.patches:
            if isinstance(a, Polygon) and a.label and not a.label.startswith('_') and \
                    not any(a is r for c in self.containers for r in c):
                hs.append(a)
        hs += [a for a in self.collections if a.label and not a.label.startswith('_')]
        return hs

    # ----- limits and ticks
    def _datalim(self, axis):
        lo = hi = None
        stick = []
        for a in self._items:
            ext = a._extent() if not isinstance(a, Text) else None
            if ext is None:
                continue
            e0, e1 = (ext[0], ext[1]) if axis == 'x' else (ext[2], ext[3])
            if e0 is None:
                continue
            lo = e0 if lo is None or e0 < lo else lo
            hi = e1 if hi is None or e1 > hi else hi
            stick += list(a.sticky_x if axis == 'x' else a.sticky_y)
        return lo, hi, sorted(stick)

    def _view(self, name):
        ax = self._axes[name]
        if ax.lim is not None:
            return ax.lim
        if name in self._cache:
            return self._cache[name]
        lo, hi, st = self._datalim(name)
        if lo is None:
            res = (0.0, 1.0)
        else:
            loc = ax.locator
            x0, x1 = loc.nonsingular(lo, hi)
            x0b = x1b = None
            if st:
                tol = 1e-5 * abs(x1 - x0)
                for v in st:
                    if v < x0 + tol:
                        x0b = v
                for v in reversed(st):
                    if v >= x1 - tol:
                        x1b = v
            delta = (x1 - x0) * matplotlib.rcParams['axes.%smargin' % name]
            x0 -= delta
            x1 += delta
            if x0b is not None:
                x0 = max(x0, x0b)
            if x1b is not None:
                x1 = min(x1, x1b)
            res = loc.view_limits(x0, x1)
        self._cache[name] = res
        return res

    def _all_labels(self, name):
        locs, shown, labs, off = self._ticks(name)
        return locs, self._lastlabels

    def _ticks(self, name):
        """(all tick locations, displayed locations, labels of the displayed ones, offset text)."""
        ax = self._axes[name]
        lo, hi = self._view(name)
        if ax.fixed_ticks is not None:
            locs = list(ax.fixed_ticks)
        elif ax.cat is not None:
            locs = [float(i) for i in _range(len(ax.cat))]
        else:
            locs = list(ax.locator.tick_values(lo, hi))
        if ax.fixed_labels is not None:
            alllab = list(ax.fixed_labels)
            off = ''
        elif ax.cat is not None:
            alllab = list(ax.cat)
            off = ''
        else:
            alllab = ax.formatter.format_ticks(locs)
            off = ax.formatter.get_offset()
        self._lastlabels = alllab
        a, b = (lo, hi) if lo <= hi else (hi, lo)
        eps = 1e-10 * (b - a)
        shown, labs = [], []
        for i in _range(len(locs)):
            if a - eps <= locs[i] <= b + eps:
                shown.append(locs[i])
                labs.append(alllab[i] if i < len(alllab) else '')
        return locs, shown, labs, off

    # ----- rendering
    def _tx(self, v, box, inv=False):
        lo, hi = self._view('x')
        if self._axes['x'].inverted:
            lo, hi = hi, lo
        return box[0] + (v - lo) / (hi - lo) * (box[1] - box[0])

    def _ty(self, v, box):
        lo, hi = self._view('y')
        if self._axes['y'].inverted:
            lo, hi = hi, lo
        return box[3] - (v - lo) / (hi - lo) * (box[3] - box[2])

    def _px(self, item, box):
        """Pixel coords of a data-space item."""
        return self._tx, self._ty

    def _svg(self, out, idn):
        box = self._box()
        x0, x1, yt, yb = box
        fig = self.figure
        k = fig.dpi / 72.0
        i = self.number
        cid = 'clip_%d' % i
        out.append('<g id="axes_%d">' % i)
        if not self._axis_off:
            out.append('<g id="patch_axes_%d"><rect x="%s" y="%s" width="%s" height="%s" style="fill:#ffffff"/></g>'
                       % (i, _n(x0), _n(yt), _n(x1 - x0), _n(yb - yt)))
        out.append('<defs><clipPath id="%s"><rect x="%s" y="%s" width="%s" height="%s"/></clipPath></defs>'
                   % (cid, _n(x0), _n(yt), _n(x1 - x0), _n(yb - yt)))
        tfs = self._tick_fs
        ticklen = 3.5 * k
        # grid + ticks
        info = {}
        for name in ('x', 'y'):
            info[name] = self._ticks(name)
        if not self._axis_off:
            for name in ('x', 'y'):
                if self._grid[name]:
                    gk = self._grid_kw
                    gc = _colors.to_hex(gk.get('color', gk.get('c', '#b0b0b0')))
                    gw = gk.get('linewidth', gk.get('lw', 0.8)) * k
                    dash = ''
                    gls = _ls_name(gk.get('linestyle', gk.get('ls', '-')))
                    if gls != '-':
                        dash = _dash(gls, gw)
                    for n_, v in enumerate(info[name][1]):
                        if name == 'x':
                            p = self._tx(v, box)
                            d = 'M %s %s L %s %s' % (_n(p), _n(yb), _n(p), _n(yt))
                        else:
                            p = self._ty(v, box)
                            d = 'M %s %s L %s %s' % (_n(x0), _n(p), _n(x1), _n(p))
                        out.append('<g id="grid_%s_%d_%d"><path d="%s" style="fill:none;stroke:%s;stroke-width:%s;'
                                   'stroke-linecap:butt%s"%s/></g>'
                                   % (name, i, n_ + 1, d, gc, _n(gw), '', dash and (' ' + dash)))
        # data artists
        out.append('<g clip-path="url(#%s)">' % cid)
        for a in self._items:
            self._draw(a, out, box, idn)
        out.append('</g>')
        if not self._axis_off:
            sw = 0.8 * k
            # spines
            for nm, d in (('left', 'M %s %s L %s %s' % (_n(x0), _n(yb), _n(x0), _n(yt))),
                          ('bottom', 'M %s %s L %s %s' % (_n(x0), _n(yb), _n(x1), _n(yb))),
                          ('right', 'M %s %s L %s %s' % (_n(x1), _n(yb), _n(x1), _n(yt))),
                          ('top', 'M %s %s L %s %s' % (_n(x0), _n(yt), _n(x1), _n(yt)))):
                out.append('<g id="spine_%s_%d"><path d="%s" style="fill:none;stroke:#000000;stroke-width:%s;'
                           'stroke-linecap:square"/></g>' % (nm, i, d, _n(sw)))
            # ticks
            xk = self._axes['x'].tick_kw
            yk = self._axes['y'].tick_kw
            xrot = xk.get('rotation', xk.get('labelrotation', 0))
            maxw = 0.0
            for n_, (v, lab) in enumerate(zip(info['x'][1], info['x'][2])):
                p = self._tx(v, box)
                out.append('<g id="xtick_%d_%d"><path d="M %s %s L %s %s" style="stroke:#000000;stroke-width:%s"/>'
                           % (i, n_ + 1, _n(p), _n(yb), _n(p), _n(yb + ticklen), _n(sw)))
                ty = yb + ticklen + 3.5 * k
                out.append(_text(lab, p, ty + 0.76 * tfs * k, tfs, 'middle', rot=xrot, ha=xk.get('ha')))
                out.append('</g>')
            xlabh = 0.97 * tfs * k
            if xrot:
                mw = max([_tw(s, tfs) for s in info['x'][2]] or [0]) * k
                r = abs(math.radians(xrot))
                xlabh = mw * math.sin(r) + 0.97 * tfs * k * math.cos(r)
            for n_, (v, lab) in enumerate(zip(info['y'][1], info['y'][2])):
                p = self._ty(v, box)
                out.append('<g id="ytick_%d_%d"><path d="M %s %s L %s %s" style="stroke:#000000;stroke-width:%s"/>'
                           % (i, n_ + 1, _n(x0), _n(p), _n(x0 - ticklen), _n(p), _n(sw)))
                out.append(_text(lab, x0 - ticklen - 3.5 * k, p + 0.36 * tfs * k, tfs, 'end'))
                out.append('</g>')
                maxw = max(maxw, _tw(lab, tfs) * k)
            # offset texts
            if info['x'][3]:
                out.append(_text(info['x'][3], x1, yb + ticklen + 3.5 * k + 0.76 * tfs * k + 1.2 * tfs * k, tfs, 'end'))
            if info['y'][3]:
                out.append(_text(info['y'][3], x0, yt - 2 * k, tfs, 'start'))
            # labels
            xl = self._axes['x']
            if xl.label:
                fs = xl.label_kw.get('fontsize', 10.0)
                ty = yb + ticklen + 3.5 * k + xlabh + 4 * k
                out.append(_text(xl.label, (x0 + x1) / 2, ty + 0.76 * fs * k, fs, 'middle'))
            yl = self._axes['y']
            if yl.label:
                fs = yl.label_kw.get('fontsize', 10.0)
                tx = x0 - ticklen - 3.5 * k - maxw - 4 * k - 0.24 * fs * k
                out.append(_text(yl.label, tx, (yt + yb) / 2, fs, 'middle', rot=90))
        if self._title:
            fs = self._title_kw.get('fontsize', 12.0)
            out.append(_text(self._title, (x0 + x1) / 2, yt - 6 * k, fs, 'middle'))
        if self._legend:
            self._draw_legend(out, box)
        out.append('</g>')

    def _draw(self, a, out, box, idn):
        k = self.figure.dpi / 72.0
        tx, ty = self._tx, self._ty
        if isinstance(a, Line2D):
            idn['line2d'] += 1
            gid = 'line2d_%d' % idn['line2d']
            if a.transform == 'xaxis':
                pts = [(box[0] + a.x[0] * (box[1] - box[0]), ty(a.y[0], box)),
                       (box[0] + a.x[1] * (box[1] - box[0]), ty(a.y[1], box))]
            elif a.transform == 'yaxis':
                pts = [(tx(a.x[0], box), box[3] - a.y[0] * (box[3] - box[2])),
                       (tx(a.x[1], box), box[3] - a.y[1] * (box[3] - box[2]))]
            else:
                pts = [(tx(a.x[j], box), ty(a.y[j], box)) if _fin(a.x[j]) and _fin(a.y[j]) else None
                       for j in _range(len(a.x))]
            out.append('<g id="%s">' % gid)
            if a.linestyle not in ('None', '', ' ') and pts:
                segs, cur = [], []
                for p in pts:
                    if p is None:
                        if cur:
                            segs.append(cur)
                        cur = []
                    else:
                        cur.append(p)
                if cur:
                    segs.append(cur)
                d = ' '.join('M ' + ' L '.join('%s %s' % (_n(p[0]), _n(p[1])) for p in s) for s in segs)
                lw = a.linewidth * k
                st = 'fill:none;stroke:%s;stroke-width:%s;stroke-linecap:%s' % (
                    a.color, _n(lw), 'butt' if a.linestyle != '-' else 'projecting')
                if a.alpha is not None:
                    st += ';stroke-opacity:%s' % _n(a.alpha)
                out.append('<path d="%s" style="%s"%s/>' % (d, st, _dash_attr(a.linestyle, lw)))
            if a.marker not in ('None', '', None):
                ms = a.markersize * k
                for p in pts:
                    if p is not None:
                        out.append(_marker(a.marker, p[0], p[1], ms, a.color, a.alpha))
            out.append('</g>')
        elif isinstance(a, Rectangle):
            idn['patch'] += 1
            xa, xb = tx(a.x, box), tx(a.x + a.w, box)
            ya, yb_ = ty(a.y, box), ty(a.y + a.h, box)
            out.append('<g id="patch_%d"><rect x="%s" y="%s" width="%s" height="%s" style="%s"/></g>'
                       % (idn['patch'], _n(min(xa, xb)), _n(min(ya, yb_)), _n(abs(xb - xa)), _n(abs(yb_ - ya)),
                          _fillstyle(a, k)))
        elif isinstance(a, Polygon):
            idn['patch'] += 1
            pts = [(tx(p[0], box), ty(p[1], box)) for p in a.pts if _fin(p[0]) and _fin(p[1])]
            d = 'M ' + ' L '.join('%s %s' % (_n(p[0]), _n(p[1])) for p in pts) + ' Z'
            out.append('<g id="patch_%d"><path d="%s" style="%s"/></g>' % (idn['patch'], d, _fillstyle(a, k)))
        elif isinstance(a, PathCollection):
            idn['coll'] += 1
            out.append('<g id="PathCollection_%d">' % idn['coll'])
            for j in _range(len(a.x)):
                if _fin(a.x[j]) and _fin(a.y[j]):
                    sz = a.s[j] if j < len(a.s) else a.s[0]
                    out.append(_marker(a.marker, tx(a.x[j], box), ty(a.y[j], box), math.sqrt(sz) * k,
                                       a.colors[j % len(a.colors)], a.alpha, edge=True))
            out.append('</g>')
        elif isinstance(a, Text):
            if a.transform == 'axes':
                px = box[0] + a.x * (box[1] - box[0])
                py = box[3] - a.y * (box[3] - box[2])
            else:
                px, py = tx(a.x, box), ty(a.y, box)
            fs = a.fontsize
            if hasattr(a, 'arrow') and a.arrow is not None:
                ax_, ay_ = tx(a.xy[0], box), ty(a.xy[1], box)
                col = _colors.to_hex(a.arrow.get('color', a.arrow.get('facecolor', 'black')))
                ang = math.atan2(ay_ - py, ax_ - px)
                hl = 8 * k
                p1 = (ax_ - hl * math.cos(ang - 0.4), ay_ - hl * math.sin(ang - 0.4))
                p2 = (ax_ - hl * math.cos(ang + 0.4), ay_ - hl * math.sin(ang + 0.4))
                out.append('<g id="annotation_arrow"><path d="M %s %s L %s %s" style="fill:none;stroke:%s;stroke-width:%s"/>'
                           '<path d="M %s %s L %s %s L %s %s Z" style="fill:%s;stroke:%s"/></g>'
                           % (_n(px), _n(py), _n(ax_), _n(ay_), col, _n(1.0 * k), _n(ax_), _n(ay_),
                              _n(p1[0]), _n(p1[1]), _n(p2[0]), _n(p2[1]), col, col))
            anchor = {'left': 'start', 'center': 'middle', 'right': 'end'}.get(a.ha, 'start')
            dy = {'center': 0.36 * fs * k, 'top': 0.76 * fs * k, 'bottom': -0.2 * fs * k}.get(a.va, 0)
            out.append(_text(a.s, px, py + dy, fs, anchor, color=a.color, rot=a.rotation, weight=a.weight))

    def _draw_legend(self, out, box):
        k = self.figure.dpi / 72.0
        kw = self._legend_kw
        fs = kw['fontsize']
        em = fs * k
        items = self._legend
        pad = 0.4 * em
        hl = 2.0 * em
        hp = 0.8 * em
        rowh = em * 0.97
        sp = 0.5 * em
        tw = max([_tw(str(l), fs) * k for h, l in items] or [0])
        title = kw.get('title')
        titleh = (rowh + sp) if title else 0
        if title:
            tw = max(tw + hl + hp, _tw(title, fs) * k) - hl - hp
        w = 2 * pad + hl + hp + tw
        h = 2 * pad + len(items) * rowh + (len(items) - 1) * sp + titleh
        loc = kw['loc']
        code = _LOCS.get(loc, loc) if isinstance(loc, str) else loc
        axp = 0.5 * em
        x0, x1, yt, yb = box

        def place(c):
            if c in (1, 4, 5, 7):
                lx = x1 - axp - w
            elif c in (2, 3, 6):
                lx = x0 + axp
            else:
                lx = (x0 + x1) / 2 - w / 2
            if c in (1, 2, 9):
                ly = yt + axp
            elif c in (3, 4, 8):
                ly = yb - axp - h
            else:
                ly = (yt + yb) / 2 - h / 2
            return lx, ly
        if code == 0:
            best = None
            for c in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10):
                lx, ly = place(c)
                bad = self._overlap_count(box, lx, ly, w, h)
                if best is None or bad < best[0]:
                    best = (bad, c)
                if bad == 0:
                    break
            code = best[1]
        lx, ly = place(code)
        out.append('<g id="legend_%d">' % self.number)
        if kw['frameon']:
            out.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" style="fill:#ffffff;fill-opacity:0.8;'
                       'stroke:#cccccc;stroke-width:%s"/>' % (_n(lx), _n(ly), _n(w), _n(h), _n(2 * k), _n(1.0 * k)))
        cy = ly + pad
        if title:
            out.append(_text(title, lx + w / 2, cy + 0.76 * em, fs, 'middle'))
            cy += titleh
        for hdl, lab in items:
            mid = cy + rowh / 2
            hx0 = lx + pad
            if isinstance(hdl, Line2D):
                if hdl.linestyle not in ('None', ''):
                    lw = hdl.linewidth * k
                    out.append('<path d="M %s %s L %s %s" style="fill:none;stroke:%s;stroke-width:%s"%s/>'
                               % (_n(hx0), _n(mid), _n(hx0 + hl), _n(mid), hdl.color, _n(lw),
                                  _dash_attr(hdl.linestyle, lw)))
                if hdl.marker not in ('None', '', None):
                    out.append(_marker(hdl.marker, hx0 + hl / 2, mid, hdl.markersize * k, hdl.color, hdl.alpha))
            elif isinstance(hdl, PathCollection):
                out.append(_marker(hdl.marker, hx0 + hl / 2, mid, math.sqrt(hdl.s[0]) * k, hdl.colors[0],
                                   hdl.alpha, edge=True))
            else:
                src = hdl[0] if isinstance(hdl, BarContainer) and len(hdl) else hdl
                out.append('<rect x="%s" y="%s" width="%s" height="%s" style="%s"/>'
                           % (_n(hx0), _n(mid - 0.35 * em), _n(hl), _n(0.7 * em), _fillstyle(src, k)))
            out.append(_text(str(lab), hx0 + hl + hp, cy + 0.76 * em, fs, 'start'))
            cy += rowh + sp
        out.append('</g>')

    def _overlap_count(self, box, lx, ly, w, h):
        n = 0
        for a in self._items:
            if isinstance(a, Line2D) and a.transform is None:
                for j in _range(len(a.x)):
                    if _fin(a.x[j]) and _fin(a.y[j]):
                        px, py = self._tx(a.x[j], box), self._ty(a.y[j], box)
                        if lx <= px <= lx + w and ly <= py <= ly + h:
                            n += 1
            elif isinstance(a, PathCollection):
                for j in _range(len(a.x)):
                    px, py = self._tx(a.x[j], box), self._ty(a.y[j], box)
                    if lx <= px <= lx + w and ly <= py <= ly + h:
                        n += 1
            elif isinstance(a, Rectangle):
                xa, xb = sorted((self._tx(a.x, box), self._tx(a.x + a.w, box)))
                ya, yb = sorted((self._ty(a.y, box), self._ty(a.y + a.h, box)))
                if xa < lx + w and xb > lx and ya < ly + h and yb > ly:
                    n += 1
        return n


def _range(*a):
    r = []
    if len(a) == 1:
        i, e, s = 0, a[0], 1
    elif len(a) == 2:
        i, e, s = a[0], a[1], 1
    else:
        i, e, s = a
    while (i < e) if s > 0 else (i > e):
        r.append(i)
        i += s
    return r


def _fsize(v):
    if isinstance(v, str):
        return {'xx-small': 5.79, 'x-small': 6.94, 'small': 8.33, 'medium': 10.0, 'large': 12.0,
                'x-large': 14.4, 'xx-large': 17.28}.get(v, 10.0)
    return float(v)


def _has_ls(fmt):
    return '-' in fmt or ':' in fmt


def _parse_fmt(fmt):
    color = marker = ls = None
    s = fmt
    for lsx in ('--', '-.', '-', ':'):
        if lsx in s:
            ls = lsx
            s = s.replace(lsx, '', 1)
            break
    for ch in s:
        if ch in 'bgrcmykw' and color is None:
            color = ch
        elif ch in 'o.,^v<>sp*hH+xDd|_1234' and marker is None:
            marker = ch
        elif ch == 'C':
            pass
    if s[:1] == 'C' and len(s) >= 2 and s[1].isdigit():
        color = s[:2]
    return color, marker, ls


_DASH = {'--': (3.7, 1.6), ':': (1.0, 1.65), '-.': (6.4, 1.6, 1.0, 1.6)}


def _dash(ls, lw_px):
    d = _DASH.get(ls)
    if not d:
        return ''
    return 'stroke-dasharray="%s"' % ','.join(_n(v * lw_px) for v in d)


def _dash_attr(ls, lw_px):
    d = _dash(ls, lw_px)
    return (' ' + d) if d else ''


def _fillstyle(a, k):
    st = 'fill:%s' % a.color
    if a.alpha is not None:
        st += ';fill-opacity:%s' % _n(a.alpha)
    if a.edgecolor != 'none':
        st += ';stroke:%s;stroke-width:%s;stroke-linejoin:miter' % (a.edgecolor, _n(a.linewidth * k))
    return st


def _marker(m, x, y, ms, color, alpha=None, edge=False):
    op = '' if alpha is None else ';fill-opacity:%s;stroke-opacity:%s' % (_n(alpha), _n(alpha))
    r = ms / 2.0
    if m in ('o', '.'):
        if m == '.':
            r = ms / 6.0
        return '<circle cx="%s" cy="%s" r="%s" style="fill:%s;stroke:%s;stroke-width:%s%s"/>' % (
            _n(x), _n(y), _n(r), color, color, _n(1.0 * 100 / 72), op)
    if m == 's':
        return '<rect x="%s" y="%s" width="%s" height="%s" style="fill:%s;stroke:%s%s"/>' % (
            _n(x - r), _n(y - r), _n(ms), _n(ms), color, color, op)
    if m in ('^', 'v', '<', '>'):
        pts = {'^': [(0, -r), (-r, r), (r, r)], 'v': [(0, r), (-r, -r), (r, -r)],
               '<': [(-r, 0), (r, -r), (r, r)], '>': [(r, 0), (-r, -r), (-r, r)]}[m]
        return '<path d="M %s Z" style="fill:%s;stroke:%s%s"/>' % (
            ' L '.join('%s %s' % (_n(x + p[0]), _n(y + p[1])) for p in pts), color, color, op)
    if m in ('D', 'd'):
        rr = r * (1.0 if m == 'D' else 0.7)
        pts = [(0, -r), (rr, 0), (0, r), (-rr, 0)]
        return '<path d="M %s Z" style="fill:%s;stroke:%s%s"/>' % (
            ' L '.join('%s %s' % (_n(x + p[0]), _n(y + p[1])) for p in pts), color, color, op)
    if m in ('+', 'x', '*', '|', '_'):
        if m == '+' or m == '*':
            d = 'M %s %s L %s %s M %s %s L %s %s' % (_n(x - r), _n(y), _n(x + r), _n(y), _n(x), _n(y - r), _n(x), _n(y + r))
        elif m == 'x':
            d = 'M %s %s L %s %s M %s %s L %s %s' % (_n(x - r), _n(y - r), _n(x + r), _n(y + r), _n(x - r), _n(y + r), _n(x + r), _n(y - r))
        elif m == '|':
            d = 'M %s %s L %s %s' % (_n(x), _n(y - r), _n(x), _n(y + r))
        else:
            d = 'M %s %s L %s %s' % (_n(x - r), _n(y), _n(x + r), _n(y))
        return '<path d="%s" style="fill:none;stroke:%s;stroke-width:%s%s"/>' % (d, color, _n(1.5 * 100 / 72), op)
    return '<circle cx="%s" cy="%s" r="%s" style="fill:%s;stroke:%s%s"/>' % (_n(x), _n(y), _n(r), color, color, op)


def _text(s, x, y, fs, anchor, color='#000000', rot=0, ha=None, weight='normal'):
    k = 100.0 / 72.0
    if ha:
        anchor = {'left': 'start', 'center': 'middle', 'right': 'end'}.get(ha, anchor)
    tr = ''
    if rot:
        tr = ' transform="rotate(%s %s %s)"' % (_n(-rot), _n(x), _n(y))
    fw = '' if weight == 'normal' else ';font-weight:%s' % weight
    return '<text x="%s" y="%s" text-anchor="%s" style="font-size:%spx;font-family:\'DejaVu Sans\',sans-serif;fill:%s%s"%s>%s</text>' % (
        _n(x), _n(y), anchor, _n(fs * k), _colors.to_hex(color), fw, tr, _esc(s))


class _Canvas:
    def __init__(self, fig):
        self.figure = fig

    def draw(self):
        for ax in self.figure.axes:
            ax._cache = {}
            ax._ticks('x')
            ax._ticks('y')


class Figure:
    def __init__(self, figsize=None, dpi=None, number=1):
        fs = figsize if figsize is not None else matplotlib.rcParams['figure.figsize']
        self.figsize = (float(fs[0]), float(fs[1]))
        self.dpi = dpi if dpi is not None else matplotlib.rcParams['figure.dpi']
        self.number = number
        self.axes = []
        self.canvas = _Canvas(self)
        self._suptitle = ''
        self._adj = {'left': 0.125, 'right': 0.9, 'bottom': 0.11, 'top': 0.88, 'wspace': 0.2, 'hspace': 0.2}

    def _wh(self):
        return self.figsize[0] * self.dpi, self.figsize[1] * self.dpi

    def get_size_inches(self):
        return _out([self.figsize[0], self.figsize[1]])

    def set_size_inches(self, w, h=None, forward=True):
        if h is None:
            w, h = w
        self.figsize = (float(w), float(h))

    def get_figwidth(self):
        return self.figsize[0]

    def get_figheight(self):
        return self.figsize[1]

    def get_dpi(self):
        return self.dpi

    def set_dpi(self, d):
        self.dpi = float(d)

    def get_axes(self):
        return list(self.axes)

    def _grid_pos(self, nrows, ncols, r, c):
        a = self._adj
        tw = a['right'] - a['left']
        th = a['top'] - a['bottom']
        cw = tw / (ncols + a['wspace'] * (ncols - 1))
        ch = th / (nrows + a['hspace'] * (nrows - 1))
        left = a['left'] + c * cw * (1 + a['wspace'])
        top = a['top'] - r * ch * (1 + a['hspace'])
        return [left, top - ch, cw, ch]

    def add_subplot(self, *args, **kw):
        if len(args) == 1:
            n = int(args[0])
            nrows, ncols, idx = n // 100, (n // 10) % 10, n % 10
        elif len(args) == 3:
            nrows, ncols, idx = args
        else:
            nrows = ncols = idx = 1
        for ax in self.axes:
            if getattr(ax, '_spec', None) == (nrows, ncols, idx):
                return ax
        r, c = divmod(idx - 1, ncols)
        ax = Axes(self, self._grid_pos(nrows, ncols, r, c), len(self.axes) + 1)
        ax._spec = (nrows, ncols, idx)
        self.axes.append(ax)
        return ax

    def add_axes(self, rect, **kw):
        ax = Axes(self, rect, len(self.axes) + 1)
        self.axes.append(ax)
        return ax

    def subplots(self, nrows=1, ncols=1, sharex=False, sharey=False, squeeze=True, **kw):
        axs = []
        for r in _range(nrows):
            for c in _range(ncols):
                ax = Axes(self, self._grid_pos(nrows, ncols, r, c), len(self.axes) + 1)
                ax._spec = (nrows, ncols, r * ncols + c + 1)
                self.axes.append(ax)
                axs.append(ax)
        if squeeze and nrows == 1 and ncols == 1:
            return axs[0]
        if squeeze and (nrows == 1 or ncols == 1):
            return _AxArray(axs, (len(axs),))
        return _AxArray(axs, (nrows, ncols))

    def subplots_adjust(self, left=None, bottom=None, right=None, top=None, wspace=None, hspace=None):
        for k, v in (('left', left), ('bottom', bottom), ('right', right), ('top', top),
                     ('wspace', wspace), ('hspace', hspace)):
            if v is not None:
                self._adj[k] = v
        for ax in self.axes:
            sp = getattr(ax, '_spec', None)
            if sp:
                r, c = divmod(sp[2] - 1, sp[1])
                ax._pos = self._grid_pos(sp[0], sp[1], r, c)

    def tight_layout(self, **kw):
        pass

    def suptitle(self, s, fontsize=None, **kw):
        self._suptitle = str(s)
        self._supfs = _fsize(fontsize) if fontsize else 12.0

    def gca(self):
        if not self.axes:
            self.add_subplot(1, 1, 1)
        return self.axes[-1]

    def clf(self):
        self.axes = []
        self._suptitle = ''

    def legend(self, *a, **kw):
        return self.gca().legend(*a, **kw)

    def to_svg(self):
        W, H = self._wh()
        out = ['<?xml version="1.0" encoding="utf-8" standalone="no"?>',
               '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="0 0 %s %s" version="1.1">'
               % (_n(W), _n(H), _n(W), _n(H)),
               '<g id="figure_%d">' % self.number,
               '<g id="patch_figure"><rect x="0" y="0" width="%s" height="%s" style="fill:#ffffff"/></g>' % (_n(W), _n(H))]
        idn = {'line2d': 0, 'patch': 0, 'coll': 0}
        for ax in self.axes:
            ax._svg(out, idn)
        if self._suptitle:
            out.append(_text(self._suptitle, W / 2, H * (1 - 0.98) + 0.76 * self._supfs * self.dpi / 72.0,
                             self._supfs, 'middle'))
        out.append('</g>')
        out.append('</svg>')
        return '\n'.join(out) + '\n'

    def savefig(self, fname, format=None, **kw):
        if isinstance(fname, str):
            ext = fname.rsplit('.', 1)[-1].lower() if '.' in fname else ''
            fmt = format or ext or 'svg'
        else:
            fmt = format or 'svg'
        if fmt != 'svg':
            raise ValueError('Format %r is not supported (supported formats: svg)' % (fmt,))
        s = self.to_svg()
        if isinstance(fname, str):
            with open(fname, 'w') as f:
                f.write(s)
        else:
            fname.write(s)


# ----- pyplot state machine
_figs = []
_cur = [None]


def figure(num=None, figsize=None, dpi=None, **kw):
    if isinstance(num, Figure):
        _cur[0] = num
        return num
    if isinstance(num, int):
        for f in _figs:
            if f.number == num:
                _cur[0] = f
                return f
    n = num if isinstance(num, int) else (max([f.number for f in _figs]) + 1 if _figs else 1)
    f = Figure(figsize, dpi, n)
    _figs.append(f)
    _cur[0] = f
    return f


def gcf():
    if _cur[0] is None:
        figure()
    return _cur[0]


def gca():
    return gcf().gca()


def subplots(nrows=1, ncols=1, figsize=None, dpi=None, **kw):
    f = figure(figsize=figsize, dpi=dpi)
    return f, f.subplots(nrows, ncols, **kw)


def subplot(*args, **kw):
    return gcf().add_subplot(*args, **kw)


def axes(*a, **kw):
    return gcf().add_axes(a[0]) if a else gca()


def subplots_adjust(**kw):
    gcf().subplots_adjust(**kw)


def tight_layout(**kw):
    pass


def close(fig=None):
    if fig is None or fig == 'all':
        del _figs[:]
        _cur[0] = None
        return
    if isinstance(fig, int):
        fig = [f for f in _figs if f.number == fig]
        fig = fig[0] if fig else None
    if fig in _figs:
        _figs.remove(fig)
    if _cur[0] is fig:
        _cur[0] = _figs[-1] if _figs else None


def clf():
    gcf().clf()


def cla():
    f = gcf()
    if f.axes:
        i = f.axes.index(f.gca())
        f.axes[i] = Axes(f, f.axes[i]._pos, f.axes[i].number)


def get_fignums():
    return [f.number for f in _figs]


def savefig(fname, **kw):
    gcf().savefig(fname, **kw)


def show(*a, **kw):
    """No window: writes figure_N.svg for every open figure."""
    for f in _figs:
        f.savefig('figure_%d.svg' % f.number)


def suptitle(s, **kw):
    gcf().suptitle(s, **kw)


def _wrap(name):
    def fn(*a, **kw):
        return getattr(gca(), name)(*a, **kw)
    fn.__name__ = name
    return fn


for _nm in ('plot', 'scatter', 'bar', 'barh', 'hist', 'pie', 'errorbar', 'fill_between', 'axhline',
            'axvline', 'text', 'annotate', 'grid', 'legend', 'axis', 'margins', 'autoscale'):
    globals()[_nm] = _wrap(_nm)


def title(s, **kw):
    return gca().set_title(s, **kw)


def xlabel(s, **kw):
    gca().set_xlabel(s, **kw)


def ylabel(s, **kw):
    gca().set_ylabel(s, **kw)


def xlim(*a, **kw):
    ax = gca()
    if a:
        return ax.set_xlim(*a, **kw)
    return ax.get_xlim()


def ylim(*a, **kw):
    ax = gca()
    if a:
        return ax.set_ylim(*a, **kw)
    return ax.get_ylim()


def xticks(ticks=None, labels=None, **kw):
    ax = gca()
    if ticks is None:
        return ax.get_xticks(), ax.get_xticklabels()
    ax.set_xticks(ticks, labels, **kw)


def yticks(ticks=None, labels=None, **kw):
    ax = gca()
    if ticks is None:
        return ax.get_yticks(), ax.get_yticklabels()
    ax.set_yticks(ticks, labels, **kw)


def tick_params(**kw):
    gca().tick_params(**kw)


class _Style:
    def use(self, *a, **kw):
        pass


style = _Style()
rcParams = matplotlib.rcParams
