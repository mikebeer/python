"""Minimal DataFrame.plot / Series.plot on top of the matplotlib subset (kind: line, bar, barh, hist,
pie, scatter, area; x=, y=, ax=, title=, legend=, stacked=, bins=, figsize=, grid=, color=, label=)."""


def _vals(s):
    out = []
    for v in s.tolist():
        out.append(float('nan') if v is None else v)
    return out


def _xpos(index):
    labs = index.tolist()
    if labs and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in labs):
        return [float(v) for v in labs], None
    return [float(i) for i in range(len(labs))], [str(v) for v in labs]


class PlotAccessor:
    def __init__(self, obj):
        self._obj = obj

    def __call__(self, x=None, y=None, kind='line', ax=None, title=None, legend=None, figsize=None,
                 stacked=False, bins=10, grid=None, color=None, label=None, xlabel=None, ylabel=None,
                 xlim=None, ylim=None, rot=None, **kw):
        import matplotlib.pyplot as plt
        obj = self._obj
        isdf = hasattr(obj, 'columns')
        if ax is None:
            ax = plt.figure(figsize=figsize).gca() if figsize else plt.gca()
        series = []
        if isdf:
            cols = list(obj.columns)
            if kind == 'scatter':
                xs, ys = obj[x], obj[y]
                ax.scatter(_vals(xs), _vals(ys), c=color)
                ax.set_xlabel(str(x))
                ax.set_ylabel(str(y))
                return self._finish(ax, title, None, grid, xlabel, ylabel, xlim, ylim)
            if y is not None:
                cols = [y] if not isinstance(y, (list, tuple)) else list(y)
            elif x is not None:
                cols = [c for c in cols if c != x]
            idx = obj[x].tolist() if x is not None else obj.index.tolist()
            for c in cols:
                series.append((str(c), _vals(obj[c])))
            index = _Idx(idx)
        else:
            nm = obj.name if obj.name is not None else None
            series.append((label if label is not None else (str(nm) if nm is not None else ''), _vals(obj)))
            index = obj.index
        showleg = isdf if legend is None else legend
        if kind in ('line', 'area'):
            xs, labs = _xpos(index)
            base = [0.0] * len(xs)
            for i, (nm, v) in enumerate(series):
                kws = {'label': nm}
                if color is not None:
                    kws['color'] = color if isinstance(color, str) else color[i % len(color)]
                if kind == 'area':
                    top = [b + (0.0 if t != t else t) for b, t in zip(base, v)] if stacked else v
                    ax.fill_between(xs, top, base if stacked else 0, alpha=kw.get('alpha', 0.5), label=nm)
                    base = top
                else:
                    ax.plot(xs, v, **kws)
            if labs is not None:
                ax.set_xticks(xs, labs)
        elif kind in ('bar', 'barh'):
            xs, labs = _xpos(index)
            n = len(series)
            if labs is None:
                labs = [str(int(v)) if v == int(v) else str(v) for v in xs]
                xs = [float(i) for i in range(len(xs))]
            width = 0.5 if (stacked or n == 1) else 0.5 / n
            bottom = [0.0] * len(xs)
            for i, (nm, v) in enumerate(series):
                kws = {'label': nm}
                if color is not None:
                    kws['color'] = color if isinstance(color, str) else color[i % len(color)]
                if stacked or n == 1:
                    pos = xs
                else:
                    pos = [p - 0.25 + width * (i + 0.5) for p in xs]
                if kind == 'bar':
                    ax.bar(pos, v, width, bottom=bottom if stacked else None, **kws)
                else:
                    ax.barh(pos, v, width, left=bottom if stacked else None, **kws)
                if stacked:
                    bottom = [b + (0.0 if t != t else t) for b, t in zip(bottom, v)]
            if kind == 'bar':
                ax.set_xticks(xs, labs)
                ax.set_xlim(-0.5, len(xs) - 0.5)
            else:
                ax.set_yticks(xs, labs)
                ax.set_ylim(-0.5, len(xs) - 0.5)
        elif kind == 'hist':
            for nm, v in series:
                ax.hist([t for t in v if t == t], bins=bins, label=nm, alpha=kw.get('alpha'))
        elif kind == 'pie':
            nm, v = series[0]
            _, labs = _xpos(index)
            ax.pie(v, labels=labs or [str(i) for i in index.tolist()], autopct=kw.get('autopct'))
            showleg = False if legend is None else legend
        else:
            raise ValueError("kind %r is not supported (line, area, bar, barh, hist, pie, scatter)" % (kind,))
        return self._finish(ax, title, showleg, grid, xlabel, ylabel, xlim, ylim)

    def _finish(self, ax, title, showleg, grid, xlabel, ylabel, xlim, ylim):
        if title:
            ax.set_title(title)
        if xlabel:
            ax.set_xlabel(xlabel)
        if ylabel:
            ax.set_ylabel(ylabel)
        if xlim:
            ax.set_xlim(*xlim)
        if ylim:
            ax.set_ylim(*ylim)
        if grid:
            ax.grid(True)
        if showleg:
            ax.legend()
        return ax

    def line(self, **kw):
        return self(kind='line', **kw)

    def bar(self, **kw):
        return self(kind='bar', **kw)

    def barh(self, **kw):
        return self(kind='barh', **kw)

    def hist(self, **kw):
        return self(kind='hist', **kw)

    def pie(self, **kw):
        return self(kind='pie', **kw)

    def area(self, **kw):
        return self(kind='area', **kw)

    def scatter(self, x=None, y=None, **kw):
        return self(x=x, y=y, kind='scatter', **kw)


class _Idx:
    def __init__(self, vals):
        self._v = list(vals)

    def tolist(self):
        return list(self._v)
