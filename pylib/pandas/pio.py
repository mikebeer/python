# pandas subset: csv input / output
from .base import nan, infer, Index, RangeIndex, MultiIndex, norm_dtype
from .core import Series, DataFrame, _lst, _astype_vals

_NA = ('', '#N/A', '#N/A N/A', '#NA', '-1.#IND', '-1.#QNAN', '-NaN', '-nan', '1.#IND', '1.#QNAN', '<NA>', 'N/A',
       'NA', 'NULL', 'NaN', 'None', 'n/a', 'nan', 'null')


def _parse_csv(text, sep, quotechar='"'):
    """split csv text into rows of string fields"""
    rows = []
    row = []
    field = []
    inq = False
    i = 0
    n = len(text)
    quoted = False
    while i < n:
        ch = text[i]
        if inq:
            if ch == quotechar:
                if i + 1 < n and text[i + 1] == quotechar:
                    field.append(quotechar)
                    i += 1
                else:
                    inq = False
            else:
                field.append(ch)
        else:
            if ch == quotechar and not field:
                inq = True
                quoted = True
            elif ch == sep:
                row.append(''.join(field))
                field = []
                quoted = False
            elif ch == '\n' or ch == '\r':
                if ch == '\r' and i + 1 < n and text[i + 1] == '\n':
                    i += 1
                if row or field or quoted:
                    row.append(''.join(field))
                    rows.append(row)
                elif False:
                    pass
                row = []
                field = []
                quoted = False
            else:
                field.append(ch)
        i += 1
    if row or field or quoted:
        row.append(''.join(field))
        rows.append(row)
    return rows


def _is_int(t):
    s = t.strip()
    if s.startswith('-') or s.startswith('+'):
        s = s[1:]
    return len(s) > 0 and s.isdigit()


def _conv_col(toks, na, dtype=None):
    vals = []
    isna = []
    for t in toks:
        if t in na:
            isna.append(True)
        else:
            isna.append(False)
    anyna = True in isna
    good = [t for t, m in zip(toks, isna) if not m]
    dn = norm_dtype(dtype) if dtype is not None else None
    if dn == 'str' or dn == 'object':
        return [nan if m else t for t, m in zip(toks, isna)], 'str' if dn == 'str' else 'object'
    kind = None
    if len(good) == 0:
        return [nan] * len(toks), 'float64'
    if dn is None or dn == 'bool':
        allb = True
        for t in good:
            if t != 'True' and t != 'False':
                allb = False
                break
        if allb:
            if anyna:
                return [nan if m else (t == 'True') for t, m in zip(toks, isna)], 'object'
            return [t == 'True' for t in toks], 'bool'
    allint = True
    for t in good:
        if not _is_int(t):
            allint = False
            break
    if allint and dn != 'float64':
        if anyna:
            return [nan if m else float(int(t)) for t, m in zip(toks, isna)], 'float64'
        return [int(t) for t in toks], 'int64'
    try:
        out = [nan if m else float(t) for t, m in zip(toks, isna)]
        for t in good:
            if t.strip().lower() in ('infinity', '-infinity', '+infinity', 'inf', '-inf', '+inf', 'nan') and False:
                pass
        return out, 'float64'
    except Exception:
        pass
    return [nan if m else t for t, m in zip(toks, isna)], 'str'


def read_csv(filepath_or_buffer, sep=',', delimiter=None, header='infer', names=None, index_col=None, usecols=None,
             dtype=None, skiprows=None, nrows=None, na_values=None, keep_default_na=True, parse_dates=None,
             skipinitialspace=False, comment=None, encoding=None, engine=None, quotechar='"', converters=None,
             true_values=None, false_values=None, thousands=None, decimal='.', skip_blank_lines=True, low_memory=None,
             on_bad_lines=None, dayfirst=False, date_format=None, **kw):
    if delimiter is not None:
        sep = delimiter
    src = filepath_or_buffer
    if not isinstance(src, str):
        text = src.read()
    else:
        f = open(src)
        text = f.read()
        f.close()
    if not isinstance(text, str):
        text = text.decode()
    if comment is not None:
        text = '\n'.join([ln.split(comment)[0] for ln in text.split('\n')])
    rows = _parse_csv(text, sep, quotechar)
    if skiprows is not None:
        if isinstance(skiprows, int):
            rows = rows[skiprows:]
        else:
            sk = set(skiprows)
            rows = [r for i, r in enumerate(rows) if i not in sk]
    if skipinitialspace:
        rows = [[x.lstrip() for x in r] for r in rows]
    na = set(_NA) if keep_default_na else set([''])
    if na_values is not None:
        if isinstance(na_values, dict):
            pass
        elif isinstance(na_values, (list, tuple, set)):
            na = na | set([str(x) for x in na_values])
        else:
            na = na | set([str(na_values)])
    if header == 'infer':
        header = 0 if names is None else None
    if header is not None and rows:
        hdr = rows[header if isinstance(header, int) else header[0]]
        rows = rows[(header if isinstance(header, int) else header[-1]) + 1:]
    else:
        hdr = None
    ncol = len(hdr) if hdr is not None else (len(rows[0]) if rows else 0)
    if names is not None:
        hdr = list(names)
        ncol = len(hdr)
    if nrows is not None:
        rows = rows[:nrows]
    # rows longer than header (index in first column) not supported; pad short rows
    if hdr is None:
        hdr = list(range(ncol))
    else:
        hdr = [h if h != '' else 'Unnamed: %d' % i for i, h in enumerate(hdr)]
        # duplicate names get .1 .2 suffix
        seen = {}
        nh = []
        for h in hdr:
            if h in seen:
                seen[h] += 1
                nh.append('%s.%d' % (h, seen[h]))
            else:
                seen[h] = 0
                nh.append(h)
        hdr = nh
    cols = []
    for j in range(ncol):
        cols.append([(r[j] if j < len(r) else '') for r in rows])
    use = list(range(ncol))
    if usecols is not None:
        if callable(usecols):
            use = [j for j in range(ncol) if usecols(hdr[j])]
        else:
            use = [j for j in range(ncol) if (hdr[j] in usecols or j in usecols)]
    vals = []
    dts = []
    outnames = []
    for j in use:
        h = hdr[j]
        dt = None
        if isinstance(dtype, dict):
            dt = dtype.get(h, dtype.get(j))
        elif dtype is not None:
            dt = dtype
        nset = na
        if isinstance(na_values, dict) and h in na_values:
            nset = na | set([str(x) for x in (na_values[h] if isinstance(na_values[h], (list, tuple, set)) else [na_values[h]])])
        if converters is not None and h in converters:
            v, d = infer([converters[h](t) for t in cols[j]])
        elif true_values or false_values:
            tv = set(true_values or [])
            fv = set(false_values or [])
            toks = ['True' if t in tv else ('False' if t in fv else t) for t in cols[j]]
            v, d = _conv_col(toks, nset, dt)
        else:
            toks = cols[j]
            if thousands is not None:
                toks = [t.replace(thousands, '') for t in toks]
            if decimal != '.':
                toks = [t.replace(decimal, '.') for t in toks]
            v, d = _conv_col(toks, nset, dt)
        dn = norm_dtype(dt) if dt is not None else None
        if dn is not None and dn != d and dn in ('int64', 'float64', 'bool'):
            v = _astype_vals(v, d, dn)
            d = dn
        vals.append(v)
        dts.append(d)
        outnames.append(h)
    df = DataFrame._fast(vals, dts, Index(outnames), RangeIndex(len(rows)))
    if parse_dates:
        from . import to_datetime
        pl = parse_dates if isinstance(parse_dates, (list, tuple)) else list(outnames)
        for c in pl:
            if c in df.columns:
                df[c] = to_datetime(df[c])
    if index_col is not None and index_col is not False:
        ic = index_col if isinstance(index_col, list) else [index_col]
        ic = [outnames[c] if isinstance(c, int) and c not in outnames else c for c in ic]
        df = df.set_index(ic if len(ic) > 1 else ic[0])
        if df.index.name is not None and str(df.index.name).startswith('Unnamed'):
            df.index.name = None
    return df


def read_table(filepath_or_buffer, sep='\t', **kw):
    return read_csv(filepath_or_buffer, sep=sep, **kw)


def _q(s, sep, quotechar='"'):
    if sep in s or quotechar in s or '\n' in s or '\r' in s:
        return quotechar + s.replace(quotechar, quotechar + quotechar) + quotechar
    return s


def _fv(x, na_rep, float_format):
    if x is None or x != x:
        return na_rep
    if isinstance(x, bool):
        return 'True' if x else 'False'
    if isinstance(x, float):
        if float_format is not None:
            return float_format % x if isinstance(float_format, str) else float_format(x)
        return repr(x)
    return str(x)


def to_csv(df, path_or_buf=None, sep=',', na_rep='', float_format=None, columns=None, header=True, index=True,
           index_label=None, mode='w', encoding=None, lineterminator=None, quotechar='"', **kw):
    if lineterminator is None:
        lineterminator = kw.get('line_terminator', '\n')
    if columns is not None:
        df = df[list(columns)]
    lines = []
    cols = df._cols._v
    ix = df._idx
    if header:
        h = []
        if index:
            if index_label is not None:
                lab = index_label if isinstance(index_label, list) else [index_label]
            elif ix.nlevels > 1:
                lab = [n if n is not None else '' for n in ix.names]
            else:
                lab = [ix.name if ix.name is not None else '']
            h.extend([str(x) for x in lab])
        if isinstance(header, list):
            h.extend([str(x) for x in header])
        elif df._cols.nlevels > 1:
            h.extend([','.join([str(y) for y in c]) for c in cols])
        else:
            h.extend([str(c) for c in cols])
        lines.append(sep.join([_q(x, sep, quotechar) for x in h]))
    n = len(ix)
    fcols = []
    for j in range(len(df._vals)):
        dt = df._dts[j]
        v = df._vals[j]
        if dt == 'str' or dt == 'object':
            fcols.append([_q(_fv(x, na_rep, float_format), sep, quotechar) for x in v])
        elif dt == 'int64':
            fcols.append([str(x) for x in v])
        else:
            fcols.append([_q(_fv(x, na_rep, float_format), sep, quotechar) for x in v])
    if index:
        if ix.nlevels > 1:
            icols = [[_q(_fv(t[k], na_rep, float_format), sep, quotechar) for t in ix._v] for k in range(ix.nlevels)]
        else:
            icols = [[_q(_fv(x, na_rep, float_format), sep, quotechar) for x in ix._v]]
        fcols = icols + fcols
    if fcols:
        for r in zip(*fcols):
            lines.append(sep.join(r))
    else:
        for i in range(n):
            lines.append('')
    out = lineterminator.join(lines) + lineterminator if lines else ''
    if path_or_buf is None:
        return out
    if not isinstance(path_or_buf, str):
        path_or_buf.write(out)
        return None
    f = open(path_or_buf, 'w' if mode == 'w' else 'a')
    f.write(out)
    f.close()
    return None


def read_json(path_or_buf, orient=None, **kw):
    import json
    if not isinstance(path_or_buf, str):
        txt = path_or_buf.read()
    elif path_or_buf.lstrip().startswith(('[', '{')):
        txt = path_or_buf
    else:
        f = open(path_or_buf)
        txt = f.read()
        f.close()
    d = json.loads(txt)
    if isinstance(d, list):
        return DataFrame(d)
    if orient in ('index',):
        return DataFrame.from_dict(d, orient='index')
    return DataFrame(d)
