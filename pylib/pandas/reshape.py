# pandas subset: concat, merge, pivot, melt, stack
import numpy as np
from .base import nan, infer, Index, RangeIndex, MultiIndex, dtype_of, sortkey_list
from .core import Series, DataFrame, _lst, _is_listlike, _NUMDT, _lred
from .groupby import DataFrameGroupBy, _fname, _std_name


def _unique(seq):
    seen = {}
    out = []
    for x in seq:
        if x not in seen:
            seen[x] = 1
            out.append(x)
    return out


def concat(objs, axis=0, join='outer', ignore_index=False, keys=None, names=None, sort=False, verify_integrity=False, copy=None):
    if isinstance(objs, dict):
        if keys is None:
            keys = list(objs.keys())
        objs = list(objs.values())
    objs = list(objs)
    pairs = [(i, o) for i, o in enumerate(objs) if o is not None]
    if keys is not None:
        keys = [keys[i] for i, o in pairs]
    objs = [o for i, o in pairs]
    if len(objs) == 0:
        raise ValueError('No objects to concatenate')
    allser = all([isinstance(o, Series) for o in objs])
    if axis in (1, 'columns'):
        return _concat_cols(objs, join, keys, ignore_index, names)
    if allser:
        vals = []
        idx = []
        for o in objs:
            vals.extend(o._v)
            idx.extend(o._idx._v)
        names0 = set([o.name for o in objs])
        nm = objs[0].name if len(names0) == 1 else None
        v, dt = infer(vals)
        if keys is not None:
            tl = []
            for k, o in zip(keys, objs):
                for x in o._idx._v:
                    tl.append((k,) + (x if isinstance(x, tuple) else (x,)))
            ix = MultiIndex(_tuples=tl, names=(names or [None]) + [o_ for o_ in (objs[0]._idx.names if objs[0]._idx.nlevels > 1 else [objs[0]._idx.name])])
        elif ignore_index:
            ix = RangeIndex(len(v))
        else:
            ix = Index(idx, name=objs[0]._idx.name) if objs[0]._idx.nlevels == 1 else MultiIndex(_tuples=idx, names=objs[0]._idx.names)
        return Series(v, ix, name=nm, _dt=dt)
    frames = []
    for o in objs:
        if isinstance(o, Series):
            o = o.to_frame()
        frames.append(o)
    cols = []
    if join == 'inner':
        cols = list(frames[0]._cols._v)
        for f in frames[1:]:
            s = set(f._cols._v)
            cols = [c for c in cols if c in s]
    else:
        seen = {}
        for f in frames:
            for c in f._cols._v:
                if c not in seen:
                    seen[c] = 1
                    cols.append(c)
        if sort:
            cols = sortkey_list(cols)
    vals = []
    dts = []
    for c in cols:
        col = []
        for f in frames:
            p = f._cols._mkpos()
            if c in p:
                col.extend(f._vals[p[c]])
            else:
                col.extend([nan] * len(f._idx))
        v, dt = infer(col)
        vals.append(v)
        dts.append(dt)
    if keys is not None:
        tl = []
        for k, f in zip(keys, frames):
            for x in f._idx._v:
                tl.append((k,) + (x if isinstance(x, tuple) else (x,)))
        ix = MultiIndex(_tuples=tl, names=(names or [None]) + (frames[0]._idx.names if frames[0]._idx.nlevels > 1 else [frames[0]._idx.name]))
    elif ignore_index:
        ix = RangeIndex(len(vals[0]) if vals else sum([len(f._idx) for f in frames]))
    else:
        idx = []
        for f in frames:
            idx.extend(f._idx._v)
        f0 = frames[0]._idx
        ix = Index(idx, name=f0.name) if f0.nlevels == 1 else MultiIndex(_tuples=idx, names=f0.names)
        if isinstance(f0, RangeIndex) and len(frames) == 1:
            ix = f0
    cx = Index(cols, name=frames[0]._cols.name) if not (cols and isinstance(cols[0], tuple)) else MultiIndex(_tuples=cols, names=frames[0]._cols.names)
    return DataFrame._fast(vals, dts, cx, ix)


def _concat_cols(objs, join, keys, ignore_index, names):
    # union of the row indexes (order of appearance)
    same = True
    for o in objs[1:]:
        if o.index._v != objs[0].index._v:
            same = False
    if same:
        idx = objs[0].index
    elif join == 'inner':
        idx = objs[0].index
        for o in objs[1:]:
            idx = idx.intersection(o.index)
    else:
        idx = objs[0].index
        for o in objs[1:]:
            idx = idx.union(o.index, sort=False)
    vals = []
    dts = []
    cols = []
    k = 0
    for oi, o in enumerate(objs):
        if isinstance(o, Series):
            sers = [(o.name if o.name is not None else k, o)]
            if keys is not None:
                sers = [(keys[oi], o)]
            k += 1
            cl = sers
        else:
            cl = [(o._cols._v[j], o._col(j)) for j in range(len(o._vals))]
            if keys is not None:
                cl = [((keys[oi], c) if True else c, s) for c, s in cl]
        for c, s in cl:
            if same:
                vals.append(list(s._v))
                dts.append(s._dt)
            else:
                p = s._idx._mkpos()
                v, dt = infer([s._v[p[x]] if x in p else nan for x in idx._v])
                vals.append(v)
                dts.append(dt)
            cols.append(c)
    if ignore_index:
        cx = RangeIndex(len(cols))
    elif cols and isinstance(cols[0], tuple):
        cx = MultiIndex(_tuples=cols, names=names or [None] * len(cols[0]))
    else:
        cx = Index(cols)
    return DataFrame._fast(vals, dts, cx, idx)


# ---------------------------------------------------------------- merge

def _nk(x):
    if x is None or x != x:
        return None
    return x


def merge(left, right, how='inner', on=None, left_on=None, right_on=None, left_index=False, right_index=False,
          sort=False, suffixes=('_x', '_y'), indicator=False):
    if isinstance(left, Series):
        left = left.to_frame()
    if isinstance(right, Series):
        right = right.to_frame()
    if on is not None:
        lon = on if isinstance(on, list) else [on]
        ron = lon
    else:
        lon = left_on if isinstance(left_on, list) else ([left_on] if left_on is not None else None)
        ron = right_on if isinstance(right_on, list) else ([right_on] if right_on is not None else None)
        if lon is None and ron is None and not left_index and not right_index and how != 'cross':
            rs = set(right._cols._v)
            lon = [c for c in left._cols._v if c in rs]
            if not lon:
                raise ValueError('No common columns to perform merge on')
            ron = lon
        if left_index and lon is None:
            lon = None
        if lon is not None and ron is None and right_index:
            ron = None
    nl = len(left._idx)
    nr = len(right._idx)
    if how == 'cross':
        lpos = [i for i in range(nl) for _ in range(nr)]
        rpos = [j for _ in range(nl) for j in range(nr)]
        lk = None
        keys_same = []
    else:
        # key arrays
        if left_index:
            lkeys = [(x,) for x in left._idx._v] if left._idx.nlevels == 1 else [t for t in left._idx._v]
            lkeys = [tuple(k) if isinstance(k, tuple) else (k,) for k in lkeys]
        else:
            arrs = [left._vals[left._colpos(c)] for c in lon]
            lkeys = list(zip(*arrs))
        if right_index:
            rkeys = [tuple(k) if isinstance(k, tuple) else (k,) for k in right._idx._v]
        else:
            arrs = [right._vals[right._colpos(c)] for c in ron]
            rkeys = list(zip(*arrs))
        lkn = [tuple([_nk(x) for x in k]) for k in lkeys]
        rkn = [tuple([_nk(x) for x in k]) for k in rkeys]
        rmap = {}
        for j in range(nr):
            k = rkn[j]
            if k in rmap:
                rmap[k].append(j)
            else:
                rmap[k] = [j]
        lpos = []
        rpos = []
        if how == 'right':
            lmap = {}
            for i in range(nl):
                k = lkn[i]
                if k in lmap:
                    lmap[k].append(i)
                else:
                    lmap[k] = [i]
            for j in range(nr):
                m = lmap.get(rkn[j])
                if m is None:
                    lpos.append(None)
                    rpos.append(j)
                else:
                    for i in m:
                        lpos.append(i)
                        rpos.append(j)
        else:
            used = {}
            for i in range(nl):
                m = rmap.get(lkn[i])
                if m is None:
                    if how in ('left', 'outer'):
                        lpos.append(i)
                        rpos.append(None)
                else:
                    for j in m:
                        lpos.append(i)
                        rpos.append(j)
                        used[j] = 1
            if how == 'outer':
                for j in range(nr):
                    if j not in used:
                        lpos.append(None)
                        rpos.append(j)
        # sort by key
        if sort or how == 'outer':
            def kk(t):
                i, j = t
                k = lkn[i] if i is not None else rkn[j]
                return k
            pairs = list(zip(lpos, rpos))
            try:
                good = [p for p in pairs if None not in kk(p)]
                bad = [p for p in pairs if None in kk(p)]
                if how == 'outer' or sort:
                    good = sorted(good, key=kk)
                pairs = good + bad
            except Exception:
                pass
            lpos = [p[0] for p in pairs]
            rpos = [p[1] for p in pairs]
    n = len(lpos)
    lnames = list(left._cols._v)
    rnames = list(right._cols._v)
    keyl = set(lon) if (lon is not None and not left_index) else set()
    keyr = set(ron) if (ron is not None and not right_index) else set()
    same_keys = (on is not None) or (lon is not None and ron is not None and lon == ron)
    # output columns
    outcols = []
    outvals = []
    ov = set()
    rset = set(rnames)
    lset = set(lnames)
    drop_r = set()
    if same_keys and not left_index and not right_index:
        drop_r = set(ron)
    for j, c in enumerate(lnames):
        v = left._vals[j]
        col = [v[i] if i is not None else nan for i in lpos]
        if same_keys and c in keyl and (how in ('right', 'outer')) and not left_index and not right_index:
            rj = right._colpos(ron[lon.index(c)])
            rv = right._vals[rj]
            col = [v[i] if i is not None else rv[r] for i, r in zip(lpos, rpos)]
        nm = c
        if c in rset and c not in drop_r and not (c in keyl and c in keyr):
            nm = str(c) + suffixes[0]
        outcols.append(nm)
        outvals.append(col)
    for j, c in enumerate(rnames):
        if c in drop_r:
            continue
        v = right._vals[j]
        col = [v[r] if r is not None else nan for r in rpos]
        nm = c
        if c in lset and not (c in keyl and c in keyr):
            nm = str(c) + suffixes[1]
        outcols.append(nm)
        outvals.append(col)
    dts = []
    for k in range(len(outvals)):
        v, dt = infer(outvals[k])
        outvals[k] = v
        dts.append(dt)
    if indicator:
        ind = []
        for i, r in zip(lpos, rpos):
            ind.append('both' if (i is not None and r is not None) else ('left_only' if i is not None else 'right_only'))
        outvals.append(ind)
        outcols.append('_merge')
        dts.append('str')
    # index
    if left_index and right_index:
        li = left._idx._v
        ri = right._idx._v
        ix = Index([li[i] if i is not None else ri[r] for i, r in zip(lpos, rpos)], name=left._idx.name)
    elif right_index and not left_index:
        li = left._idx._v
        ix = Index([li[i] if i is not None else nan for i in lpos], name=left._idx.name) if how != 'right' else Index([right._idx._v[r] for r in rpos], name=right._idx.name)
        if how == 'left' or how == 'inner':
            ix = Index([li[i] for i in lpos], name=left._idx.name)
        else:
            ix = RangeIndex(n)
    elif left_index and not right_index:
        ri = right._idx._v
        if how in ('right', 'inner'):
            ix = Index([ri[r] for r in rpos], name=right._idx.name)
        else:
            ix = RangeIndex(n)
    else:
        ix = RangeIndex(n)
    cx = Index(outcols)
    return DataFrame._fast(outvals, dts, cx, ix)


# ---------------------------------------------------------------- pivot / melt / stack

def _aslist(x):
    if x is None:
        return None
    return list(x) if isinstance(x, (list, tuple)) else [x]


def pivot_table(df, values=None, index=None, columns=None, aggfunc='mean', fill_value=None, margins=False,
                dropna=True, margins_name='All'):
    idxn = _aslist(index) or []
    coln = _aslist(columns) or []
    keys = idxn + coln
    if values is None:
        vals = [c for j, c in enumerate(df._cols._v) if c not in keys and (df._dts[j] in _NUMDT or aggfunc in ('count', 'nunique', 'size', 'first', 'last', 'min', 'max'))]
    else:
        vals = _aslist(values)
    if isinstance(aggfunc, dict):
        funcs = None
        vals = list(aggfunc.keys())
    else:
        funcs = aggfunc if isinstance(aggfunc, (list, tuple)) else [aggfunc]
    multi_f = isinstance(aggfunc, (list, tuple))
    gb = DataFrameGroupBy(df, keys, None, True, True, dropna)
    ri = len(idxn)
    # per (func, value) -> dict key -> result
    results = []
    for v in vals:
        j = df._colpos(v)
        fl = _aslist(aggfunc[v]) if funcs is None else funcs
        for f in fl:
            col = gb._agg_one(j, f)
            results.append((_fname(f), v, dict(zip(gb._order, col))))
    kgroups = gb._order

    def split(k):
        if not isinstance(k, tuple):
            k = (k,)
        return k[:ri], k[ri:]
    rowkeys = []
    colkeys = []
    for k in kgroups:
        a, b = split(k)
        if a not in rowkeys:
            rowkeys.append(a)
        if b not in colkeys:
            colkeys.append(b)
    rowkeys = sortkey_list(rowkeys)
    colkeys = sortkey_list(colkeys)
    if not idxn:
        rowkeys = [()]
    if not coln:
        colkeys = [()]
    outvals = []
    outcols = []
    for fname, v, d in results:
        for ck in colkeys:
            col = []
            for rk in rowkeys:
                k = rk + ck
                if len(k) == 1:
                    k = k[0]
                col.append(d.get(k, nan))
            outvals.append(col)
            label = []
            if multi_f or (funcs is None and False):
                label.append(fname)
            if len(vals) > 1 or not coln or (funcs is None and False):
                if not (len(vals) == 1 and coln and not multi_f):
                    label.append(v)
            label.extend(list(ck))
            outcols.append(tuple(label) if len(label) > 1 else (label[0] if label else v))
    if not idxn:
        rix = Index([vals[0]] if False else ['__x__'])
    if len(idxn) == 1:
        rix = Index([k[0] for k in rowkeys], name=idxn[0])
    elif len(idxn) > 1:
        rix = MultiIndex(_tuples=rowkeys, names=idxn)
    names = []
    if outcols and isinstance(outcols[0], tuple):
        nl = len(outcols[0])
        names = [None] * nl
        if coln:
            for q in range(len(coln)):
                names[nl - len(coln) + q] = coln[q]
        cx = MultiIndex(_tuples=outcols, names=names)
    else:
        cx = Index(outcols)
        if len(coln) == 1:
            cx.name = coln[0]
    dv = []
    dd = []
    for c in outvals:
        if fill_value is not None:
            c = [fill_value if x != x else x for x in c]
        v, dt = infer(c)
        dv.append(v)
        dd.append(dt)
    res = DataFrame._fast(dv, dd, cx, rix)
    if margins and len(idxn) == 1 and len(coln) <= 1 and len(vals) == 1 and not multi_f:
        f = funcs[0]
        j = df._colpos(vals[0])
        vcol = df._vals[j]
        rows = {}
        allv = [vcol[i] for i in range(len(df._idx))]
        lastcol = []
        for rk in rowkeys:
            lastcol.append(_agg_list(f, [vcol[i] for i in range(len(df._idx)) if (df._vals[df._colpos(idxn[0])][i],) == rk]))
        newvals = []
        for c in range(len(res._vals)):
            ck = colkeys[c] if coln else ()
            sel = [vcol[i] for i in range(len(df._idx)) if (not coln or (df._vals[df._colpos(coln[0])][i],) == ck)]
            newvals.append(list(res._vals[c]) + [_agg_list(f, sel)])
        if coln:
            newvals.append(lastcol + [_agg_list(f, allv)])
            cn = list(res._cols._v) + [margins_name]
            cx2 = Index(cn, name=coln[0])
        else:
            cx2 = res._cols
        rix2 = Index(list(rix._v) + [margins_name], name=idxn[0])
        dv = []
        dd = []
        for c in newvals:
            v, dt = infer(c)
            dv.append(v)
            dd.append(dt)
        res = DataFrame._fast(dv, dd, cx2, rix2)
    if not idxn:
        # only columns given: transpose-like (rare)
        pass
    return res


def _agg_list(f, vals):
    nm = _std_name(f)
    if nm is not None:
        return _lred(nm, vals)
    return f(Series(vals))


def pivot(df, index=None, columns=None, values=None):
    ci = df._colpos(columns)
    ii = df._colpos(index) if index is not None else None
    vl = _aslist(values) if values is not None else [c for c in df._cols._v if c != columns and c != index]
    rows = _unique(df._vals[ii]) if ii is not None else list(df._idx._v)
    cvals = _unique(df._vals[ci])
    try:
        rows = sorted(rows)
        cvals = sorted(cvals)
    except Exception:
        pass
    rp = {k: n for n, k in enumerate(rows)}
    cp = {k: n for n, k in enumerate(cvals)}
    rk = df._vals[ii] if ii is not None else list(df._idx._v)
    outvals = []
    outcols = []
    for v in vl:
        vj = df._colpos(v)
        grid = [[nan] * len(rows) for _ in cvals]
        for i in range(len(df._idx)):
            grid[cp[df._vals[ci][i]]][rp[rk[i]]] = df._vals[vj][i]
        for k, c in enumerate(cvals):
            outvals.append(grid[k])
            outcols.append((v, c) if len(vl) > 1 else c)
    dv = []
    dd = []
    for c in outvals:
        v, dt = infer(c)
        dv.append(v)
        dd.append(dt)
    if len(vl) > 1:
        cx = MultiIndex(_tuples=outcols, names=[None, columns])
    else:
        cx = Index(outcols, name=columns)
    return DataFrame._fast(dv, dd, cx, Index(rows, name=index))


def melt(df, id_vars=None, value_vars=None, var_name=None, value_name='value'):
    ids = _aslist(id_vars) or []
    vv = _aslist(value_vars) if value_vars is not None else [c for c in df._cols._v if c not in ids]
    n = len(df._idx)
    cols = {}
    for c in ids:
        cols[c] = list(df._vals[df._colpos(c)]) * len(vv)
    vn = var_name if var_name is not None else 'variable'
    cols[vn] = [c for c in vv for _ in range(n)]
    allv = []
    for c in vv:
        allv.extend(df._vals[df._colpos(c)])
    cols[value_name] = allv
    return DataFrame(cols)


def stack(df):
    rows = []
    idx = []
    nm = df._idx.name
    for i in range(len(df._idx)):
        for j in range(len(df._vals)):
            x = df._vals[j][i]
            if x == x:
                rows.append(x)
                lab = df._idx._v[i]
                idx.append((lab if not isinstance(lab, tuple) else lab) , ) if False else idx.append(((lab,) if not isinstance(lab, tuple) else lab) + (df._cols._v[j],))
    names = (df._idx.names if df._idx.nlevels > 1 else [df._idx.name]) + [df._cols.name]
    return Series(rows, MultiIndex(_tuples=idx, names=names))


def unstack(obj, level=-1):
    if isinstance(obj, DataFrame):
        s = stack(obj)
        return unstack(s, level)
    ix = obj._idx
    if ix.nlevels < 2:
        raise ValueError('index must be a MultiIndex to unstack')
    last = ix.nlevels - 1 if level == -1 else level
    rows = []
    cols = []
    for t in ix._v:
        r = tuple([t[k] for k in range(ix.nlevels) if k != last])
        if r not in rows:
            rows.append(r)
        if t[last] not in cols:
            cols.append(t[last])
    try:
        rows = sorted(rows)
        cols = sorted(cols)
    except Exception:
        pass
    rp = {k: n for n, k in enumerate(rows)}
    cp = {k: n for n, k in enumerate(cols)}
    grid = [[nan] * len(rows) for _ in cols]
    for t, v in zip(ix._v, obj._v):
        r = tuple([t[k] for k in range(ix.nlevels) if k != last])
        grid[cp[t[last]]][rp[r]] = v
    dv = []
    dd = []
    for c in grid:
        v, dt = infer(c)
        dv.append(v)
        dd.append(dt)
    keepnames = [ix.names[k] for k in range(ix.nlevels) if k != last]
    if len(keepnames) == 1:
        rix = Index([r[0] for r in rows], name=keepnames[0])
    else:
        rix = MultiIndex(_tuples=rows, names=keepnames)
    return DataFrame._fast(dv, dd, Index(cols, name=ix.names[last]), rix)


def crosstab(index, columns, values=None, rownames=None, colnames=None, aggfunc=None, margins=False, margins_name='All', dropna=True, normalize=False):
    iname = getattr(index, 'name', None) or 'row_0'
    cname = getattr(columns, 'name', None) or 'col_0'
    il = list(_lst(index))
    cl = list(_lst(columns))
    rows = sortkey_list(_unique(il))
    cols = sortkey_list(_unique(cl))
    rp = {k: n for n, k in enumerate(rows)}
    cp = {k: n for n, k in enumerate(cols)}
    grid = [[0] * len(rows) for _ in cols]
    for a, b in zip(il, cl):
        grid[cp[b]][rp[a]] += 1
    if margins:
        for c in range(len(cols)):
            grid[c].append(sum(grid[c]))
        grid.append([sum([grid[c][r] for c in range(len(cols))]) for r in range(len(rows) + 1)])
        rows = rows + [margins_name]
        cols = cols + [margins_name]
    if normalize is True or normalize == 'all':
        tot = float(sum([sum(g) for g in grid])) if not margins else float(grid[-1][-1])
        grid = [[x / tot for x in g] for g in grid]
    elif normalize == 'index':
        grid = [[grid[c][r] / float(sum([grid[k][r] for k in range(len(cols))])) for r in range(len(rows))] for c in range(len(cols))]
    elif normalize == 'columns':
        grid = [[x / float(sum(g)) for x in g] for g in grid]
    dv = []
    dd = []
    for c in grid:
        v, dt = infer(c)
        dv.append(v)
        dd.append(dt)
    return DataFrame._fast(dv, dd, Index(cols, name=cname), Index(rows, name=iname))


def get_dummies(data, prefix=None, prefix_sep='_', dummy_na=False, columns=None, sparse=False, drop_first=False, dtype=None):
    if isinstance(data, Series):
        cats = sortkey_list(_unique([x for x in data._v if x == x]))
        if drop_first:
            cats = cats[1:]
        cols = {}
        for c in cats:
            nm = (prefix + prefix_sep + str(c)) if prefix is not None else c
            cols[nm] = [x == c for x in data._v]
        r = DataFrame(cols, index=data._idx)
        if dtype is not None:
            r = r.astype(dtype)
        return r
    cl = columns if columns is not None else [c for j, c in enumerate(data._cols._v) if data._dts[j] in ('str', 'object')]
    out = data.drop(columns=cl)
    for c in cl:
        d = get_dummies(data[c], prefix=c if prefix is None else prefix, prefix_sep=prefix_sep, drop_first=drop_first, dtype=dtype)
        for j, nm in enumerate(d._cols._v):
            out[nm] = d._col(j)
    return out
