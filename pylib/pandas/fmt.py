# pandas subset: text layout of Index / Series / DataFrame like pandas' repr
from .base import nan

MAX_ROWS = 60
MIN_ROWS = 10
WIDTH = 80
MAX_COLWIDTH = 50


def _sci(vals):
    out = []
    for v in vals:
        if v != v:
            out.append('NaN')
        else:
            a = -v if v < 0 else v
            out.append(('-' if v < 0 else ' ') + '%.6e' % a)
    return out


def fmt_floats(vals, digits=6):
    """strings for floats with a sign slot, common decimals (pandas FloatArrayFormatter)"""
    large = False
    small = False
    lim = 10.0 ** (-digits)
    out = []
    for v in vals:
        if v != v:
            out.append('NaN')
            continue
        a = -v if v < 0 else v
        if a > 1e6:
            large = True
        if a > 0 and a < lim:
            small = True
        t = '%.6f' % v
        if t[:2] == '--':
            t = t[1:]
        if t[0] != '-':
            t = ' ' + t
        out.append(t)
    if small:
        return _sci(vals)
    # trim common trailing zeros (keep one decimal digit)
    z = 100
    for s in out:
        if s == 'NaN' or 'inf' in s:
            continue
        k = 0
        n = len(s)
        while n - 1 - k >= 0 and s[n - 1 - k] == '0':
            k += 1
        if k < z:
            z = k
        if z == 0:
            break
    res = []
    maxlen = 0
    for s in out:
        if s == 'NaN' or 'inf' in s:
            res.append(s)
            continue
        if z > 0 and z < 100:
            s = s[:len(s) - z]
        if s.endswith('.'):
            s = s + '0'
        if len(s) > maxlen:
            maxlen = len(s)
        res.append(s)
    if large and maxlen > digits + 6:
        return _sci(vals)
    return res


def _objstr(x):
    if x is None:
        return 'None'
    if isinstance(x, str):
        return x
    if isinstance(x, float):
        if x != x:
            return 'NaN'
        return str(x)
    if isinstance(x, (list, tuple)):
        return ('[' if isinstance(x, list) else '(') + ', '.join([_objstr(e) for e in x]) + (']' if isinstance(x, list) else ')')
    return str(x)


def fmt_cells(vals, dt):
    """cell strings (sign / leading slot included), not yet padded to a common width"""
    if dt == 'int64':
        return [('-%d' % -x) if x < 0 else (' %d' % x) for x in vals]
    if dt == 'float64':
        return fmt_floats(vals)
    if dt == 'bool':
        return [' True' if x else ' False' for x in vals]
    if dt == 'str':
        res = []
        for x in vals:
            if x != x:
                res.append(' NaN')
            else:
                res.append(' ' + x)
        return _trunc(res)
    if dt.startswith('datetime'):
        return [' ' + s for s in dt_strings(vals)]
    if dt.startswith('timedelta'):
        return [' ' + s for s in td_strings(vals)]
    return _trunc([' ' + _objstr(x) for x in vals])


def _trunc(cells):
    out = []
    for s in cells:
        if len(s) > MAX_COLWIDTH:
            s = s[:MAX_COLWIDTH - 3] + '...'
        out.append(s)
    return out


def dt_strings(vals):
    full = False
    micro = False
    for x in vals:
        if x == x:
            if x.hour or x.minute or x.second or x.microsecond:
                full = True
            if x.microsecond:
                micro = True
    out = []
    for x in vals:
        if x != x:
            out.append('NaT')
        elif micro:
            out.append(str(x) if x.microsecond else str(x) + '.000000')
        elif full:
            out.append(str(x)[:19])
        else:
            out.append(str(x)[:10])
    return out


def td_strings(vals):
    full = False
    micro = False
    for x in vals:
        if x == x:
            if x.seconds or x.microseconds:
                full = True
            if x.microseconds:
                micro = True
    out = []
    for x in vals:
        if x != x:
            out.append('NaT')
            continue
        d = x.days
        s = x.seconds
        t = '%d days' % d
        if full:
            t += ' %02d:%02d:%02d' % (s // 3600, (s // 60) % 60, s % 60)
            if micro:
                t += '.%06d' % x.microseconds
        out.append(t)
    return out


def format_labels(v, dt):
    if dt.startswith('datetime'):
        return dt_strings(v)
    if dt.startswith('timedelta'):
        return td_strings(v)
    if dt == 'float64':
        return [s[1:] if s.startswith(' ') else s for s in fmt_floats(v)]
    if dt == 'int64' or dt == 'bool':
        return [str(x) for x in v]
    out = []
    for x in v:
        if isinstance(x, float) and x != x:
            out.append('NaN')
        elif x is None:
            out.append('None')
        else:
            out.append(str(x))
    return out


def _just_left(strs, minimum=0):
    w = minimum
    for s in strs:
        if len(s) > w:
            w = len(s)
    return [s + ' ' * (w - len(s)) for s in strs], w


def _just_right(strs, minimum=0):
    w = minimum
    for s in strs:
        if len(s) > w:
            w = len(s)
    return [' ' * (w - len(s)) + s for s in strs], w


def _center(s, width):
    n = len(s)
    if n >= width:
        return s
    marg = width - n
    left = marg // 2 + (marg & width & 1)
    return ' ' * left + s + ' ' * (marg - left)


def series_repr(s):
    n = len(s._v)
    nm = s.name
    foot = ''
    if nm is not None:
        foot = 'Name: %s, ' % _namestr(nm)
    if n == 0:
        return 'Series([], %sdtype: %s)' % (foot, s._dt)
    trunc = n > MAX_ROWS
    idx = s.index
    if trunc:
        half = min(MIN_ROWS, MAX_ROWS) // 2
        pos = list(range(half)) + list(range(n - half, n))
        vals = [s._v[i] for i in pos]
        foot += 'Length: %d, ' % n
    else:
        pos = None
        vals = s._v
    head = ''
    if idx.nlevels == 1:
        labs = _index_strings_sel(idx, pos)
        if idx.name is not None:
            head = _namestr(idx.name) + '\n'
        labs, lw = _just_left(labs)
    else:
        cols = _mi_cols(idx, pos)
        have = any([x is not None for x in idx.names])
        if have:
            cols = [[_lab(idx.names[k]) if idx.names[k] is not None else ''] + cols[k] for k in range(len(cols))]
        cols = [_just_left(c)[0] for c in cols]
        rows = ['  '.join([c[r] for c in cols]) for r in range(len(cols[0]))]
        if have:
            head = rows[0] + '\n'
            rows = rows[1:]
        labs = rows
        lw = len(rows[0])
    cells = fmt_cells(vals, s._dt)
    cells, w = _just_right(cells)
    if trunc:
        if w > 3:
            dots = _center('...', w)
        else:
            dots = _center('..', w)
        cells = cells[:half] + [dots] + cells[half:]
        labs = labs[:half] + [' ' * lw] + labs[half:]
    lines = []
    for i in range(len(cells)):
        lines.append(labs[i] + '   ' + cells[i])
    return head + '\n'.join(lines) + '\n' + foot + 'dtype: ' + s._dt


def _namestr(x):
    return str(x)


def _index_strings_sel(idx, pos):
    """display strings of a single level index (selected positions)"""
    if pos is None:
        return idx._format_labels()
    sub = [idx._v[i] for i in pos]
    return format_labels(sub, idx._dt)


def _mi_cols(idx, pos):
    """per level list of label strings, sparsified, left justified"""
    v = idx._v if pos is None else [idx._v[i] for i in pos]
    nl = idx.nlevels
    cols = []
    prev = None
    strs = []
    for t in v:
        strs.append([_lab(x) for x in t])
    for k in range(nl):
        col = []
        for r in range(len(v)):
            if r > 0 and v[r][:k + 1] == v[r - 1][:k + 1]:
                col.append('')
            else:
                col.append(strs[r][k])
        cols.append(col)
    return cols


def _lab(x):
    if isinstance(x, float) and x != x:
        return 'NaN'
    return str(x)


def _col_header_cells(columns, j, numeric):
    """header lines for column j"""
    if columns.nlevels == 1:
        lab = columns._v[j]
        return [(' ' if numeric else '') + _lab(lab)]
    t = columns._v[j]
    return [_lab(x) for x in t]


def frame_repr(df):
    ncols = len(df.columns)
    nrows = len(df.index)
    if ncols == 0 or nrows == 0:
        cl = ', '.join([_lab(c) for c in df.columns._v])
        il = ', '.join([_lab(c) for c in df.index._v])
        return 'Empty DataFrame\nColumns: [%s]\nIndex: [%s]' % (cl, il)
    row_trunc = nrows > MAX_ROWS
    half = min(MIN_ROWS, MAX_ROWS) // 2
    if row_trunc:
        pos = list(range(half)) + list(range(nrows - half, nrows))
    else:
        pos = None
    # index block
    idx = df.index
    cols = df.columns
    ncl = cols.nlevels
    if idx.nlevels > 1:
        levels = _mi_cols(idx, pos)
        names = [(_lab(x) if x is not None else '') for x in idx.names]
        have_names = any([x is not None for x in idx.names])
    else:
        if pos is None:
            levels = [idx._format_labels()]
        else:
            levels = [_index_strings_sel(idx, pos)]
        have_names = idx.name is not None
        names = [_lab(idx.name) if have_names else '']
    lvl_out = []
    for k in range(len(levels)):
        c = levels[k]
        if have_names:
            c = [names[k]] + c
        c, w = _just_left(c)
        lvl_out.append(c)
    nrow_idx = len(lvl_out[0])
    idxcol = []
    for r in range(nrow_idx):
        idxcol.append(' '.join([lc[r] for lc in lvl_out]))
    colname_hdr = [''] * ncl
    if cols.nlevels == 1 and cols.name is not None:
        colname_hdr = [_lab(cols.name)]
    elif cols.nlevels > 1:
        colname_hdr = [(_lab(x) if x is not None else '') for x in cols.names]
    idxcol = colname_hdr + idxcol
    idxcol, iw = _just_left(idxcol)
    # data columns
    strcols = [idxcol]
    for j in range(ncols):
        sv = df._vals[j]
        dt = df._dts[j]
        vals = sv if pos is None else [sv[i] for i in pos]
        cells = fmt_cells(vals, dt)
        numeric = dt == 'int64' or dt == 'float64' or dt == 'bool'
        hdr = _col_header_cells(cols, j, numeric)
        if cols.nlevels > 1:
            hdr = _sparse_hdr(cols, j, hdr, numeric)
        allc = hdr + cells
        if have_names:
            allc = hdr + [''] + cells
        allc, w = _just_right(allc)
        strcols.append(allc)
    nhdr = ncl + (1 if have_names else 0)
    # horizontal fit
    lines_w = 0
    for c in strcols:
        lines_w += len(c[0]) + 1
    lines_w -= 1
    col_trunc = False
    if lines_w - WIDTH + 1 > 0:
        lens = [(i, len(strcols[i][0])) for i in range(len(strcols))]
        adj = lines_w - WIDTH + 1
        n = len(lens)
        while adj > 0 and n > 1:
            mid = round(n / 2)
            adj -= lens[mid][1] + 1
            lens.pop(mid)
            n = len(lens)
        fitted = n - 1
        if fitted < 2:
            fitted = 2
        chalf = fitted // 2
        if chalf >= 1 and chalf * 2 < ncols:
            col_trunc = True
            left = strcols[:chalf + 1]
            right = strcols[ncols - chalf + 1:]
            dotc = [' ...'] * len(strcols[0])
            strcols = left + [dotc] + right
            for i in range(len(strcols)):
                pass
    # vertical dots
    if row_trunc:
        for i in range(len(strcols)):
            c = strcols[i]
            if col_trunc and c is not None and len(c) > 0 and c[0] == ' ...' and i == chalf + 1:
                dot = '...'
                cw = 4
            else:
                cw = len(c[0])
                dot = '...' if cw > 3 else '..'
            if i == 0:
                d = dot + ' ' * (cw - len(dot))
            else:
                d = ' ' * (cw - len(dot)) + dot
            c.insert(nhdr + half, d)
            if i == 0:
                w = max([len(x) for x in c])
                strcols[0] = [x + ' ' * (w - len(x)) for x in c]
    lines = []
    for r in range(len(strcols[0])):
        lines.append(' '.join([c[r] for c in strcols]))
    out = '\n'.join(lines)
    if row_trunc or col_trunc:
        out += '\n\n[%d rows x %d columns]' % (nrows, ncols)
    return out


def _sparse_hdr(cols, j, hdr, numeric):
    t = cols._v[j]
    out = []
    for k in range(len(t)):
        if j > 0 and cols._v[j - 1][:k + 1] == t[:k + 1] and k < len(t) - 1:
            out.append('')
        else:
            out.append(hdr[k])
    return out
