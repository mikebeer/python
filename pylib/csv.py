"""csv: reader, writer, DictReader, DictWriter (pure Python)."""

QUOTE_MINIMAL = 0
QUOTE_ALL = 1
QUOTE_NONNUMERIC = 2
QUOTE_NONE = 3
QUOTE_STRINGS = 4
QUOTE_NOTNULL = 5


class Error(Exception):
    pass


class Dialect:
    delimiter = ","
    quotechar = '"'
    escapechar = None
    doublequote = True
    skipinitialspace = False
    lineterminator = "\r\n"
    quoting = QUOTE_MINIMAL
    strict = False


class excel(Dialect):
    pass


class excel_tab(excel):
    delimiter = "\t"


class unix_dialect(Dialect):
    lineterminator = "\n"
    quoting = QUOTE_ALL


_dialects = {"excel": excel, "excel-tab": excel_tab, "unix": unix_dialect}


def register_dialect(name, dialect=None, **kw):
    _dialects[name] = _Params(dialect, kw)


def unregister_dialect(name):
    del _dialects[name]


def get_dialect(name):
    return _dialects[name]


def list_dialects():
    return list(_dialects)


class _Params:
    def __init__(self, dialect, kw):
        if isinstance(dialect, str):
            dialect = _dialects[dialect]
        base = dialect if dialect is not None else excel
        self.delimiter = kw.get("delimiter", getattr(base, "delimiter", ","))
        self.quotechar = kw.get("quotechar", getattr(base, "quotechar", '"'))
        self.escapechar = kw.get("escapechar", getattr(base, "escapechar", None))
        self.doublequote = kw.get("doublequote", getattr(base, "doublequote", True))
        self.skipinitialspace = kw.get("skipinitialspace", getattr(base, "skipinitialspace", False))
        self.lineterminator = kw.get("lineterminator", getattr(base, "lineterminator", "\r\n"))
        self.quoting = kw.get("quoting", getattr(base, "quoting", QUOTE_MINIMAL))
        self.strict = kw.get("strict", getattr(base, "strict", False))
        if self.quotechar is None and self.quoting != QUOTE_NONE:
            self.quoting = QUOTE_NONE


class _Reader:
    def __init__(self, iterable, dialect="excel", **kw):
        self._p = _Params(dialect, kw)
        self._it = iter(iterable)
        self.line_num = 0
        self.dialect = self._p

    def __iter__(self):
        return self

    def __next__(self):
        p = self._p
        line = next(self._it)
        self.line_num += 1
        q = p.quotechar
        d = p.delimiter
        esc = p.escapechar
        if (q is None or q not in line) and (esc is None or esc not in line):
            if line.endswith("\r\n"):
                line = line[:-2]
            elif line.endswith("\n") or line.endswith("\r"):
                line = line[:-1]
            if line == "":
                return []
            row = line.split(d)
            if p.skipinitialspace:
                row = [f.lstrip(" ") for f in row]
            if p.quoting == QUOTE_NONNUMERIC:
                row = [float(f) if f != "" else f for f in row]
            return row
        row = []
        field = []
        quoted = False
        inq = False
        i = 0
        started = False
        while True:
            n = len(line)
            while i < n:
                c = line[i]
                i += 1
                if inq:
                    if esc is not None and c == esc and i < n:
                        field.append(line[i])
                        i += 1
                    elif c == q:
                        if p.doublequote and i < n and line[i] == q:
                            field.append(q)
                            i += 1
                        else:
                            inq = False
                    else:
                        field.append(c)
                else:
                    if c == d:
                        row.append(self._fin(field, quoted))
                        field = []
                        quoted = False
                        started = False
                        if p.skipinitialspace:
                            while i < n and line[i] == " ":
                                i += 1
                    elif c == q and q is not None and not field and not quoted:
                        inq = True
                        quoted = True
                        started = True
                    elif c == q and q is not None and p.quoting != QUOTE_NONE and p.strict:
                        raise Error("',' expected after '\"'")
                    elif esc is not None and c == esc and i < n:
                        field.append(line[i])
                        i += 1
                    elif c == "\n" or c == "\r":
                        pass
                    else:
                        field.append(c)
                        started = True
            if inq:
                try:
                    line = next(self._it)
                except StopIteration:
                    if p.strict:
                        raise Error("unexpected end of data")
                    break
                self.line_num += 1
                i = 0
                continue
            break
        row.append(self._fin(field, quoted))
        return row

    def _fin(self, field, quoted):
        s = "".join(field)
        if self._p.quoting == QUOTE_NONNUMERIC and not quoted and s != "":
            return float(s)
        return s


def reader(csvfile, dialect="excel", **kw):
    return _Reader(csvfile, dialect, **kw)


class _Writer:
    def __init__(self, fileobj, dialect="excel", **kw):
        self._p = _Params(dialect, kw)
        self._f = fileobj
        self.dialect = self._p

    def _quote(self, v, single):
        p = self._p
        if v is None:
            s = ""
            isnum = False
        elif isinstance(v, str):
            s = v
            isnum = False
        elif isinstance(v, float):
            s = repr(v)
            isnum = True
        else:
            s = str(v)
            isnum = isinstance(v, int)
        quoting = p.quoting
        need = False
        if quoting == QUOTE_ALL:
            need = True
        elif quoting == QUOTE_NONNUMERIC:
            need = not isnum
        elif quoting == QUOTE_STRINGS:
            need = isinstance(v, str)
        elif quoting == QUOTE_NOTNULL:
            need = v is not None
        d = p.delimiter
        q = p.quotechar
        esc = p.escapechar
        if quoting == QUOTE_NONE:
            out = []
            for c in s:
                if c == d or c == esc or (q is not None and c == q) or c == "\n" or c == "\r":
                    if esc is None:
                        raise Error("need to escape, but no escapechar set")
                    out.append(esc)
                out.append(c)
            return "".join(out)
        if not need:
            if s == "" and single:
                need = True
            elif d in s or (q is not None and q in s) or "\n" in s or "\r" in s or \
                    (esc is not None and esc in s):
                need = True
            else:
                for c in p.lineterminator:
                    if c in s:
                        need = True
        if not need:
            return s
        if q in s:
            if p.doublequote:
                s = s.replace(q, q + q)
            elif esc is not None:
                s = s.replace(q, esc + q)
            else:
                raise Error("need to escape, but no escapechar set")
        return q + s + q

    def writerow(self, row):
        row = list(row)
        single = len(row) == 1
        line = self._p.delimiter.join([self._quote(v, single) for v in row]) + self._p.lineterminator
        return self._f.write(line)

    def writerows(self, rows):
        for r in rows:
            self.writerow(r)


def writer(csvfile, dialect="excel", **kw):
    return _Writer(csvfile, dialect, **kw)


class DictReader:
    def __init__(self, f, fieldnames=None, restkey=None, restval=None, dialect="excel", *args, **kw):
        self._fieldnames = fieldnames
        self.restkey = restkey
        self.restval = restval
        self.reader = reader(f, dialect, *args, **kw)
        self.dialect = dialect
        self.line_num = 0

    def __iter__(self):
        return self

    @property
    def fieldnames(self):
        if self._fieldnames is None:
            try:
                self._fieldnames = next(self.reader)
            except StopIteration:
                pass
        self.line_num = self.reader.line_num
        return self._fieldnames

    @fieldnames.setter
    def fieldnames(self, value):
        self._fieldnames = value

    def __next__(self):
        if self.line_num == 0:
            self.fieldnames
        row = next(self.reader)
        self.line_num = self.reader.line_num
        while row == []:
            row = next(self.reader)
        fn = self._fieldnames
        d = {}
        lf = len(fn)
        lr = len(row)
        for i in range(lf):
            d[fn[i]] = row[i] if i < lr else self.restval
        if lr > lf:
            d[self.restkey] = row[lf:]
        return d


class DictWriter:
    def __init__(self, f, fieldnames, restval="", extrasaction="raise", dialect="excel", *args, **kw):
        self.fieldnames = fieldnames
        self.restval = restval
        if extrasaction.lower() not in ("raise", "ignore"):
            raise ValueError("extrasaction (%s) must be 'raise' or 'ignore'" % extrasaction)
        self.extrasaction = extrasaction
        self.writer = writer(f, dialect, *args, **kw)

    def writeheader(self):
        return self.writerow(dict(zip(self.fieldnames, self.fieldnames)))

    def _dict_to_list(self, rowdict):
        if self.extrasaction == "raise":
            wrong = [k for k in rowdict if k not in self.fieldnames]
            if wrong:
                raise ValueError("dict contains fields not in fieldnames: " + ", ".join([repr(x) for x in wrong]))
        return [rowdict.get(key, self.restval) for key in self.fieldnames]

    def writerow(self, rowdict):
        return self.writer.writerow(self._dict_to_list(rowdict))

    def writerows(self, rowdicts):
        for r in rowdicts:
            self.writerow(r)
