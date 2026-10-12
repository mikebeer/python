"""Tick locators and formatters ported from matplotlib (MaxNLocator, AutoLocator, ScalarFormatter)."""
import math
import matplotlib


def nonsingular(vmin, vmax, expander=0.001, tiny=1e-15, increasing=True):
    if not (math.isfinite(vmin) and math.isfinite(vmax)):
        return -expander, expander
    swapped = False
    if vmax < vmin:
        vmin, vmax = vmax, vmin
        swapped = True
    mx = max(abs(vmin), abs(vmax))
    if mx < (1e6 / tiny) * 2.2250738585072014e-308:
        vmin = -expander
        vmax = expander
    elif vmax - vmin <= mx * tiny:
        if vmax == 0 and vmin == 0:
            vmin = -expander
            vmax = expander
        else:
            vmin -= expander * abs(vmin)
            vmax += expander * abs(vmax)
    if swapped and not increasing:
        vmin, vmax = vmax, vmin
    return vmin, vmax


def scale_range(vmin, vmax, n=1, threshold=100):
    dv = abs(vmax - vmin)
    meanv = (vmax + vmin) / 2
    if abs(meanv) / dv < threshold:
        offset = 0
    else:
        offset = math.copysign(10 ** (math.log10(abs(meanv)) // 1), meanv)
    scale = 10 ** (math.log10(dv / n) // 1)
    return scale, offset


class _Edge_integer:
    def __init__(self, step, offset):
        if step <= 0:
            raise ValueError("'step' must be positive")
        self.step = step
        self._offset = abs(offset)

    def closeto(self, ms, edge):
        if self._offset > 0:
            digits = math.log10(self._offset / self.step)
            tol = max(1e-10, 10 ** (digits - 12))
            tol = min(0.4999, tol)
        else:
            tol = 1e-10
        return abs(ms - edge) < tol

    def le(self, x):
        d, m = divmod(x, self.step)
        if self.closeto(m / self.step, 1):
            return d + 1
        return d

    def ge(self, x):
        d, m = divmod(x, self.step)
        if self.closeto(m / self.step, 0):
            return d
        return d + 1


class Locator:
    axis = None

    def nonsingular(self, v0, v1):
        return nonsingular(v0, v1, expander=0.05)

    def set_axis(self, axis):
        self.axis = axis


class MaxNLocator(Locator):
    def __init__(self, nbins=10, steps=None, integer=False, symmetric=False,
                 prune=None, min_n_ticks=2):
        self._nbins = nbins if nbins == 'auto' else int(nbins)
        self._symmetric = symmetric
        self._prune = prune
        self._min_n_ticks = max(1, min_n_ticks)
        if steps is None:
            steps = [1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10]
        steps = list(steps)
        if steps[0] != 1:
            steps = [1] + steps
        if steps[-1] != 10:
            steps = steps + [10]
        self._steps = steps
        self._extended_steps = [0.1 * s for s in steps[:-1]] + steps + [10 * steps[1]]
        self._integer = integer

    def _raw_ticks(self, vmin, vmax):
        if self._nbins == 'auto':
            if self.axis is not None:
                nbins = self.axis.get_tick_space()
                nbins = int(min(max(nbins, max(1, self._min_n_ticks - 1)), 9))
            else:
                nbins = 9
        else:
            nbins = self._nbins
        scale, offset = scale_range(vmin, vmax, nbins)
        _vmin = vmin - offset
        _vmax = vmax - offset
        steps = [s * scale for s in self._extended_steps]
        if self._integer:
            steps = [s for s in steps if s < 1 or abs(s - round(s)) < 0.001]
        raw_step = (_vmax - _vmin) / nbins
        istep = len(steps) - 1
        for i in range(len(steps)):
            if steps[i] >= raw_step:
                istep = i
                break
        ticks = []
        for k in range(istep, -1, -1):
            step = steps[k]
            if self._integer and math.floor(_vmax) - math.ceil(_vmin) >= self._min_n_ticks - 1:
                step = max(1, step)
            best_vmin = (_vmin // step) * step
            edge = _Edge_integer(step, offset)
            low = edge.le(_vmin - best_vmin)
            high = edge.ge(_vmax - best_vmin)
            n = int(high - low) + 1
            ticks = [(low + i) * step + best_vmin for i in range(n)]
            nt = 0
            for t in ticks:
                if _vmin <= t <= _vmax:
                    nt += 1
            if nt >= self._min_n_ticks:
                break
        return [t + offset for t in ticks]

    def tick_values(self, vmin, vmax):
        if self._symmetric:
            vmax = max(abs(vmin), abs(vmax))
            vmin = -vmax
        vmin, vmax = nonsingular(vmin, vmax, expander=1e-13, tiny=1e-14)
        locs = self._raw_ticks(vmin, vmax)
        if self._prune == 'lower':
            locs = locs[1:]
        elif self._prune == 'upper':
            locs = locs[:-1]
        elif self._prune == 'both':
            locs = locs[1:-1]
        return locs

    def __call__(self):
        vmin, vmax = self.axis.get_view_interval()
        return self.tick_values(vmin, vmax)

    def view_limits(self, dmin, dmax):
        if self._symmetric:
            dmax = max(abs(dmin), abs(dmax))
            dmin = -dmax
        return nonsingular(dmin, dmax, expander=1e-12, tiny=1e-13)


class AutoLocator(MaxNLocator):
    def __init__(self):
        MaxNLocator.__init__(self, nbins='auto', steps=[1, 2, 2.5, 5, 10])


class FixedLocator(Locator):
    def __init__(self, locs):
        self.locs = [float(v) for v in locs]

    def __call__(self):
        return list(self.locs)


class Formatter:
    axis = None

    def set_axis(self, axis):
        self.axis = axis

    def set_locs(self, locs):
        self.locs = locs

    def format_ticks(self, values):
        self.set_locs(values)
        return [self(v, i) for i, v in enumerate(values)]

    def get_offset(self):
        return ''


class FuncFormatter(Formatter):
    def __init__(self, func):
        self.func = func

    def __call__(self, x, pos=None):
        return self.func(x, pos)


class FixedFormatter(Formatter):
    def __init__(self, seq):
        self.seq = list(seq)

    def __call__(self, x, pos=None):
        if pos is None or pos >= len(self.seq):
            return ''
        return self.seq[pos]


class StrMethodFormatter(Formatter):
    def __init__(self, fmt):
        self.fmt = fmt

    def __call__(self, x, pos=None):
        return self.fmt.format(x=x, pos=pos)


class NullFormatter(Formatter):
    def __call__(self, x, pos=None):
        return ''


def _minus(s):
    if matplotlib.rcParams.get('axes.unicode_minus', True):
        return s.replace('-', '−')
    return s


class ScalarFormatter(Formatter):
    def __init__(self, useOffset=None, useMathText=None, useLocale=None):
        if useOffset is None:
            useOffset = matplotlib.rcParams['axes.formatter.useoffset']
        self._offset_threshold = matplotlib.rcParams['axes.formatter.offset_threshold']
        self.set_useOffset(useOffset)
        self._orderOfMagnitude = 0
        self._format = ''
        self._scientific = True
        self._powerlimits = matplotlib.rcParams['axes.formatter.limits']
        self.locs = []

    def set_useOffset(self, val):
        if isinstance(val, bool):
            self.offset = 0
            self._useOffset = val
        else:
            self._useOffset = False
            self.offset = val

    def set_scientific(self, b):
        self._scientific = bool(b)

    def set_powerlimits(self, lims):
        self._powerlimits = lims

    def __call__(self, x, pos=None):
        if len(self.locs) == 0:
            return ''
        xp = (x - self.offset) / (10. ** self._orderOfMagnitude)
        if abs(xp) < 1e-8:
            xp = 0
        return _minus(self._format % xp)

    def format_data(self, value):
        e = math.floor(math.log10(abs(value)))
        s = round(value / 10 ** e, 10)
        significand = '%d' % s if s % 1 == 0 else '%1.10g' % s
        if e == 0:
            return significand
        return '%se%d' % (significand, e)

    def get_offset(self):
        if len(self.locs) == 0:
            return ''
        if self._orderOfMagnitude or self.offset:
            offsetStr = ''
            sciNotStr = ''
            if self.offset:
                offsetStr = self.format_data(self.offset)
                if self.offset > 0:
                    offsetStr = '+' + offsetStr
            if self._orderOfMagnitude:
                sciNotStr = '1e%d' % self._orderOfMagnitude
            return _minus(sciNotStr + offsetStr)
        return ''

    def set_locs(self, locs):
        self.locs = list(locs)
        if len(self.locs) > 0:
            if self._useOffset:
                self._compute_offset()
            self._set_order_of_magnitude()
            self._set_format()

    def _visible(self):
        vmin, vmax = sorted(self.axis.get_view_interval())
        return [v for v in self.locs if vmin <= v <= vmax]

    def _compute_offset(self):
        locs = self._visible()
        if not locs:
            self.offset = 0
            return
        lmin, lmax = min(locs), max(locs)
        if lmin == lmax or lmin <= 0 <= lmax:
            self.offset = 0
            return
        abs_min, abs_max = sorted([abs(float(lmin)), abs(float(lmax))])
        sign = math.copysign(1, lmin)
        oom_max = int(math.ceil(math.log10(abs_max)))
        oom = oom_max
        while not (abs_min // 10 ** oom != abs_max // 10 ** oom):
            oom -= 1
        oom += 1
        if (abs_max - abs_min) / 10 ** oom <= 1e-2:
            oom = oom_max
            while not (abs_max // 10 ** oom - abs_min // 10 ** oom > 1):
                oom -= 1
            oom += 1
        n = self._offset_threshold - 1
        if abs_max // 10 ** oom >= 10 ** n:
            self.offset = sign * (abs_max // 10 ** oom) * 10 ** oom
        else:
            self.offset = 0

    def _set_order_of_magnitude(self):
        if not self._scientific:
            self._orderOfMagnitude = 0
            return
        if self._powerlimits[0] == self._powerlimits[1] != 0:
            self._orderOfMagnitude = self._powerlimits[0]
            return
        vmin, vmax = sorted(self.axis.get_view_interval())
        locs = [abs(v) for v in self._visible()]
        if not locs:
            self._orderOfMagnitude = 0
            return
        if self.offset:
            oom = math.floor(math.log10(vmax - vmin))
        else:
            val = max(locs)
            if val == 0:
                oom = 0
            else:
                oom = math.floor(math.log10(val))
        if oom <= self._powerlimits[0]:
            self._orderOfMagnitude = oom
        elif oom >= self._powerlimits[1]:
            self._orderOfMagnitude = oom
        else:
            self._orderOfMagnitude = 0

    def _set_format(self):
        if len(self.locs) < 2:
            _locs = list(self.locs) + list(self.axis.get_view_interval())
        else:
            _locs = self.locs
        p = 10. ** self._orderOfMagnitude
        locs = [(v - self.offset) / p for v in _locs]
        loc_range = max(locs) - min(locs)
        if loc_range == 0:
            loc_range = max([abs(v) for v in locs])
        if loc_range == 0:
            loc_range = 1
        if len(self.locs) < 2:
            locs = locs[:-2]
        loc_range_oom = int(math.floor(math.log10(loc_range)))
        sigfigs = max(0, 3 - loc_range_oom)
        thresh = 1e-3 * 10 ** loc_range_oom
        while sigfigs >= 0:
            if max([abs(v - round(v, sigfigs)) for v in locs]) < thresh:
                sigfigs -= 1
            else:
                break
        sigfigs += 1
        self._format = '%%1.%df' % sigfigs
