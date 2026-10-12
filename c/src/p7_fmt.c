/* =========================================================================
   p7_fmt.c   format() mini language, % formatting, str.format
   ========================================================================= */
typedef struct FSpec {
    int fill, align, sign, alt, zero, comma, under, pct;
    long width, prec;
    int type;
} FSpec;

static void parse_spec(const char *s, long n, FSpec *sp)
{
    long i = 0;
    sp->fill = ' '; sp->align = 0; sp->sign = 0; sp->alt = 0; sp->zero = 0;
    sp->comma = 0; sp->under = 0; sp->pct = 0; sp->width = -1; sp->prec = -1; sp->type = 0;
    if (n >= 2 && strchr("<>^=", s[1])) { sp->fill = s[0]; sp->align = s[1]; i = 2; }
    else if (n >= 1 && strchr("<>^=", s[0])) { sp->align = s[0]; i = 1; }
    if (i < n && strchr("+- ", s[i])) { sp->sign = s[i]; i++; }
    if (i < n && s[i] == '#') { sp->alt = 1; i++; }
    if (i < n && s[i] == '0') { sp->zero = 1; i++; }
    if (i < n && ch_digit[(unsigned char)s[i]]) {
        sp->width = 0;
        while (i < n && ch_digit[(unsigned char)s[i]]) sp->width = sp->width * 10 + (s[i++] - '0');
    }
    if (i < n && s[i] == ',') { sp->comma = 1; i++; }
    if (i < n && s[i] == '_') { sp->under = 1; i++; }
    if (i < n && s[i] == '.') {
        i++;
        sp->prec = 0;
        while (i < n && ch_digit[(unsigned char)s[i]]) sp->prec = sp->prec * 10 + (s[i++] - '0');
    }
    if (i < n) sp->type = (unsigned char)s[i++];
    if (i < n) throw_new(cls_ValueError, "Invalid format specifier");
}

/* digits d[0..nd) with a separator between groups of g (3 for decimal, 4 for x o b).  With
   minw > 0 (zero padding) leading zeros are added until the output plus 'other' characters
   outside it reach minw, as CPython does (so a separator never leads) */
static void group_run(Buf *out, const char *d, long nd, int sepc, int g, long other, long minw)
{
    long nz = 0, i, n2, len;
    if (minw > 0) {
        for (;;) {
            n2 = nd + nz;
            len = n2 + (n2 - 1) / g;
            if (len + other >= minw) break;
            nz++;
        }
    }
    n2 = nd + nz;
    for (i = 0; i < n2; i++) {
        if (i > 0 && (n2 - i) % g == 0) buf_addc(out, sepc);
        buf_addc(out, i < nz ? '0' : d[i - nz]);
    }
}

static Value pad_result(FSpec *sp, const char *sign, const char *prefix, const char *body,
                        long blen, int defalign)
{
    Buf out;
    long total = (long)strlen(sign) + (long)strlen(prefix) + blen, pad = 0, i;
    int align = sp->align ? sp->align : defalign, fill = sp->fill;
    if (sp->width > total) pad = sp->width - total;
    buf_init(&out);
    if (sp->zero && !sp->align) { align = '='; fill = '0'; }
    if (align == '<') {
        buf_add(&out, sign); buf_add(&out, prefix); buf_addn(&out, body, blen);
        for (i = 0; i < pad; i++) buf_addc(&out, fill);
    } else if (align == '>') {
        for (i = 0; i < pad; i++) buf_addc(&out, fill);
        buf_add(&out, sign); buf_add(&out, prefix); buf_addn(&out, body, blen);
    } else if (align == '^') {
        long l = pad / 2;
        for (i = 0; i < l; i++) buf_addc(&out, fill);
        buf_add(&out, sign); buf_add(&out, prefix); buf_addn(&out, body, blen);
        for (i = 0; i < pad - l; i++) buf_addc(&out, fill);
    } else {
        buf_add(&out, sign); buf_add(&out, prefix);
        for (i = 0; i < pad; i++) buf_addc(&out, fill);
        buf_addn(&out, body, blen);
    }
    return buf_value(&out);
}

static const char *sign_str(FSpec *sp, int neg)
{
    if (neg) return "-";
    if (sp->sign == '+') return "+";
    if (sp->sign == ' ') return " ";
    return "";
}

static Value format_float_spec(double d, FSpec *sp)
{
    char *tmp, fm[16];
    Buf body;
    int neg = 0, t = sp->type, k;
    long prec = sp->prec, other;
    Value r;
    if (d != d) {
        return pad_result(sp, sign_str(sp, 0), "", "nan", 3, '>');
    }
    if (d < 0 || (d == 0 && 1.0 / d < 0)) { neg = 1; d = -d; }
    if (d > DBL_MAX) {
        return pad_result(sp, sign_str(sp, neg), "",
            t == 'F' || t == 'E' || t == 'G' ? "INF" : "inf", 3, '>');
    }
    if (prec > 1000000L) throw_new(cls_ValueError, "precision too big");
    tmp = (char *)xmalloc((size_t)(440 + (prec > 0 ? prec : 0)));
    if (t == 0) {
        if (prec < 0) { float_repr(d, tmp); }
        else {
            sprintf(tmp, sp->alt ? "%#.*g" : "%.*g", (int)(prec == 0 ? 1L : prec), d);
            if (!strchr(tmp, 'e') && !strchr(tmp, 'n')) {
                /* no type: the exponent form starts one digit earlier than with 'g' */
                char *ep;
                int pr = (int)(prec == 0 ? 1L : prec);
                sprintf(tmp, "%.*e", pr - 1, d);
                ep = strchr(tmp, 'e');
                if (ep && atoi(ep + 1) == pr - 1 && d != 0) {
                    if (!sp->alt) {
                        char *m = ep;
                        while (m > tmp && m[-1] == '0') m--;
                        if (m > tmp && m[-1] == '.') m--;
                        memmove(m, ep, strlen(ep) + 1);
                    }
                } else {
                    sprintf(tmp, sp->alt ? "%#.*g" : "%.*g", pr, d);
                    if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'n'))
                        strcat(tmp, ".0");
                }
            }
        }
    } else if (t == '%') {
        sprintf(tmp, sp->alt ? "%#.*f" : "%.*f", (int)(prec < 0 ? 6L : prec), d * 100.0);
        strcat(tmp, "%");
    } else {
        int ct = t;
        if (ct == 'F') ct = 'f';
        if (ct == 'n') ct = 'g';
        k = 0;
        fm[k++] = '%';
        if (sp->alt) fm[k++] = '#';
        fm[k++] = '.'; fm[k++] = '*'; fm[k++] = (char)ct; fm[k] = 0;
        sprintf(tmp, fm, (int)(prec < 0 ? 6L : prec), d);
    }
    buf_init(&body);
    if (sp->comma || sp->under) {
        long intlen = 0, tl = (long)strlen(tmp);
        while (intlen < tl && ch_digit[(unsigned char)tmp[intlen]]) intlen++;
        other = (long)strlen(sign_str(sp, neg)) + (tl - intlen);
        group_run(&body, tmp, intlen, sp->comma ? ',' : '_', 3, other,
                  sp->zero && !sp->align ? sp->width : 0);
        buf_add(&body, tmp + intlen);
    } else buf_add(&body, tmp);
    r = pad_result(sp, sign_str(sp, neg), "", body.s, body.n, '>');
    buf_free(&body);
    free(tmp);
    return r;
}

/* set by the % operator: a precision on an integer conversion means a minimum digit count */
static int fmt_pct_mode;

static Value format_int_spec(Value v, FSpec *sp)
{
    Buf body, g;
    int neg = int_sign(v) < 0, isdec;
    const char *prefix = "";
    Value r, av = neg ? int_neg(v) : v;
    buf_init(&body);
    switch (sp->type) {
    case 'x': case 'X': int_str_base(av, 16, &body); prefix = sp->alt ? "0x" : ""; break;
    case 'o': int_str_base(av, 8, &body); prefix = sp->alt ? "0o" : ""; break;
    case 'b': int_str_base(av, 2, &body); prefix = sp->alt ? "0b" : ""; break;
    default: int_str(av, &body); break;
    }
    isdec = !(sp->type == 'x' || sp->type == 'X' || sp->type == 'o' || sp->type == 'b');
    if (sp->type == 'X') {
        long i;
        for (i = 0; i < body.n; i++) body.s[i] = (char)to_up[(unsigned char)body.s[i]];
        if (prefix[0]) prefix = "0X";
    }
    if (sp->pct && sp->prec > body.n) {          /* "%.5d": at least 5 digits */
        long i, z = sp->prec - body.n;
        buf_init(&g);
        for (i = 0; i < z; i++) buf_addc(&g, '0');
        buf_addn(&g, body.s, body.n);
        buf_free(&body);
        body = g;
    }
    if (sp->comma || sp->under) {
        buf_init(&g);
        group_run(&g, body.s, body.n, sp->comma ? ',' : '_', isdec ? 3 : 4,
                  (long)strlen(sign_str(sp, neg)) + (long)strlen(prefix),
                  sp->zero && !sp->align ? sp->width : 0);
        buf_free(&body);
        body = g;
    }
    r = pad_result(sp, sign_str(sp, neg), prefix, body.s, body.n, '>');
    buf_free(&body);
    return r;
}

/* format() of a complex number: [(]real imag j[)] with the spec applied to both parts */
static Value format_complex_spec(Value v, FSpec *sp)
{
    double re = AS_CPLX(v)->re, im = AS_CPLX(v)->im;
    int type = sp->type, paren = 0, skip_re = 0;
    long prec = sp->prec;
    Buf out;
    Value r, part;
    FSpec ps;
    if (sp->align == '=')
        throw_new(cls_ValueError, "'=' alignment flag is not allowed in complex format specifier");
    if (sp->zero)
        throw_new(cls_ValueError, "Zero padding is not allowed in complex format specifier");
    if (type != 0 && !strchr("eEfFgGn", type))
        throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'complex'", type);
    if (type == 0) {
        type = 'r';
        if (re == 0.0 && !signbit_d(re)) skip_re = 1; else paren = 1;
    }
    if (type == 'n') type = 'g';
    if (prec >= 0 && type == 'r') type = 'g';
    buf_init(&out);
    if (paren) buf_addc(&out, '(');
    ps = *sp;
    ps.width = -1; ps.zero = 0; ps.align = 0; ps.fill = ' '; ps.prec = prec;
    ps.type = type == 'r' ? 0 : type;
    if (!skip_re) {
        if (type == 'r') {
            Buf t;
            int neg = signbit_d(re) && re == re;
            buf_init(&t);
            complex_part(neg ? -re : re, &t);
            buf_add(&out, sign_str(sp, neg));
            buf_addn(&out, t.s, t.n);
            buf_free(&t);
        } else {
            part = format_float_spec(re, &ps);
            buf_addn(&out, AS_STR(part)->s, AS_STR(part)->len);
        }
    }
    {
        int neg = signbit_d(im) && im == im;
        const char *sg;
        sg = skip_re ? sign_str(sp, neg) : (neg ? "-" : "+");
        if (type == 'r') {
            Buf t;
            buf_init(&t);
            complex_part(neg ? -im : im, &t);
            buf_add(&out, sg);
            buf_addn(&out, t.s, t.n);
            buf_free(&t);
        } else {
            ps.sign = skip_re ? sp->sign : '+';
            part = format_float_spec(im, &ps);
            buf_addn(&out, AS_STR(part)->s, AS_STR(part)->len);
        }
    }
    buf_addc(&out, 'j');
    if (paren) buf_addc(&out, ')');
    {
        FSpec ws = *sp;
        ws.zero = 0;
        r = pad_result(&ws, "", "", out.s, out.n, '>');
    }
    buf_free(&out);
    return r;
}

static Value format_value(Value v, const char *spec, long speclen)
{
    FSpec sp;
    int pct = fmt_pct_mode;
    fmt_pct_mode = 0;
    if (speclen == 0) {
        if (v.t == T_STR) return v;
        return py_str(v);
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_format);
        if (m) return call2(*m, v, mkstr(spec, speclen));
        throw_new(cls_TypeError, "unsupported format string passed to %.60s.__format__",
            type_name(v));
    }
    parse_spec(spec, speclen, &sp);
    sp.pct = pct;
    if (v.t == T_STR || (sp.type == 's')) {
        Value s = v.t == T_STR ? v : py_str(v);
        long len = AS_STR(s)->len;
        if (sp.type != 0 && sp.type != 's')
            throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'str'", sp.type);
        if (sp.prec >= 0 && sp.prec < len) len = sp.prec;
        return pad_result(&sp, "", "", AS_STR(s)->s, len, '<');
    }
    if (v.t == T_BOOL && sp.type == 0) return format_value(py_str(v), spec, speclen);
    if (v.t == T_INT || v.t == T_BOOL || v.t == T_BIG) {
        int t = sp.type;
        if (sp.comma && sp.under) throw_new(cls_ValueError, "Cannot specify both ',' and '_'.");
        if (sp.comma && t != 0 && t != 'd' && t != 'e' && t != 'E' && t != 'f' && t != 'F' &&
            t != 'g' && t != 'G' && t != '%')
            throw_new(cls_ValueError, "Cannot specify ',' with '%c'.", t);
        if (sp.under && (t == 'c' || t == 'n' || t == 's'))
            throw_new(cls_ValueError, "Cannot specify '_' with '%c'.", t);
        if (t == 'c') {
            Buf b;
            Value r;
            if (v.t == T_BIG || v.u.i < 0 || v.u.i > 0x10FFFFL)
                throw_new(cls_OverflowError, "%%c arg not in range(0x110000)");
            buf_init(&b);
            put_utf8(&b, (unsigned long)v.u.i);
            r = pad_result(&sp, "", "", b.s, b.n, '<');
            buf_free(&b);
            return r;
        }
        if (t == 0 || t == 'd' || t == 'n' || t == 'x' || t == 'X' || t == 'o' || t == 'b') {
            if (sp.prec >= 0 && !sp.pct)
                throw_new(cls_ValueError, "Precision not allowed in integer format specifier");
            return format_int_spec(v, &sp);
        }
        if (strchr("eEfFgG%", t)) return format_float_spec(to_dbl(v), &sp);
        throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'int'", t);
    }
    if (v.t == T_COMPLEX) return format_complex_spec(v, &sp);
    if (v.t == T_FLOAT) {
        int t = sp.type;
        if (sp.under && t == 'n') throw_new(cls_ValueError, "Cannot specify '_' with 'n'.");
        if (sp.comma && t == 'n') throw_new(cls_ValueError, "Cannot specify ',' with 'n'.");
        if (t == 0 || t == 'n' || strchr("eEfFgG%", t)) return format_float_spec(v.u.d, &sp);
        throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'float'", t);
    }
    throw_new(cls_TypeError, "unsupported format string passed to %.60s.__format__", type_name(v));
    return NONE_V;
}

/* ---- % formatting ----------------------------------------------------- */
static Value py_format_percent(Value fmt, Value args)
{
    Str *f = AS_STR(fmt);
    Buf out;
    long i = 0, argi = 0;
    List *tup;
    int used_single = 0;
    if (args.t == T_INST && (AS_INST(args)->pay.t == T_TUPLE || AS_INST(args)->pay.t == T_DICT))
        args = AS_INST(args)->pay;
    tup = args.t == T_TUPLE ? AS_LIST(args) : NULL;
    buf_init(&out);
    while (i < f->len) {
        int c = f->s[i++];
        char spec[64];
        int sl = 0, flags_minus = 0, flags_zero = 0, flags_plus = 0, flags_space = 0, flags_alt = 0;
        long width = -1, prec = -1;
        Value arg, res;
        int t;
        if (c != '%') { buf_addc(&out, c); continue; }
        if (i >= f->len) throw_new(cls_ValueError, "incomplete format");
        arg = NONE_V;
        if (f->s[i] == '(') {
            long st = ++i;
            Value key, d = args;
            while (i < f->len && f->s[i] != ')') i++;
            key = mkstr(f->s + st, i - st);
            i++;
            if (d.t != T_DICT) throw_new(cls_TypeError, "format requires a mapping");
            if (!dict_get(AS_DICT(d), key, &arg)) throw_key(key);
            used_single = 2;
        }
        while (i < f->len && strchr("-+ #0", f->s[i])) {
            switch (f->s[i]) {
            case '-': flags_minus = 1; break;
            case '+': flags_plus = 1; break;
            case ' ': flags_space = 1; break;
            case '#': flags_alt = 1; break;
            default: flags_zero = 1; break;
            }
            i++;
        }
        if (i < f->len && f->s[i] == '*') {
            Value w;
            i++;
            if (!tup || argi >= tup->n)
                throw_new(cls_TypeError, "not enough arguments for format string");
            w = tup->a[argi++];
            width = need_long(w, "width");
            if (width < 0) { flags_minus = 1; width = -width; }
        } else {
            while (i < f->len && ch_digit[(unsigned char)f->s[i]]) {
                if (width < 0) width = 0;
                width = width * 10 + (f->s[i++] - '0');
            }
        }
        if (i < f->len && f->s[i] == '.') {
            i++;
            prec = 0;
            if (i < f->len && f->s[i] == '*') {
                i++;
                if (!tup || argi >= tup->n)
                    throw_new(cls_TypeError, "not enough arguments for format string");
                prec = need_long(tup->a[argi++], "precision");
            } else {
                while (i < f->len && ch_digit[(unsigned char)f->s[i]])
                    prec = prec * 10 + (f->s[i++] - '0');
            }
        }
        while (i < f->len && strchr("lhL", f->s[i])) i++;
        if (i >= f->len) throw_new(cls_ValueError, "incomplete format");
        t = (unsigned char)f->s[i++];
        if (t == '%') { buf_addc(&out, '%'); continue; }
        if (used_single != 2) {
            if (tup) {
                if (argi >= tup->n)
                    throw_new(cls_TypeError, "not enough arguments for format string");
                arg = tup->a[argi++];
            } else {
                if (argi >= 1) throw_new(cls_TypeError, "not enough arguments for format string");
                arg = args;
                argi++;
                used_single = 1;
            }
        }
        /* build a format spec for format_value */
        if (flags_minus) spec[sl++] = '<';
        else if (strchr("sr", t) || t == 'a') spec[sl++] = '>';
        if (flags_plus) spec[sl++] = '+';
        else if (flags_space) spec[sl++] = ' ';
        if (flags_alt) spec[sl++] = '#';
        if (flags_zero && !flags_minus && !strchr("sra", t)) spec[sl++] = '0';
        if (width >= 0) sl += sprintf(spec + sl, "%ld", width);
        if (prec >= 0) sl += sprintf(spec + sl, ".%ld", prec);
        if (t != 'r' && t != 'a' && t != 's' && t != 'c') arg = unpay(arg);
        if (t == 'r' || t == 'a') { arg = py_repr(arg); t = 's'; }
        else if (t == 's') { arg = py_str(arg); }
        else if (t == 'i' || t == 'u') t = 'd';
        if (t == 'd' || t == 'x' || t == 'X' || t == 'o') {
            if (arg.t == T_FLOAT)
                arg = int_from_double(arg.u.d < 0 ? ceil(arg.u.d) : floor(arg.u.d));
            else if (!is_intv(arg))
                throw_new(cls_TypeError, "%%%c format: a real number is required, not %.40s", t,
                    type_name(arg));
        } else if (strchr("eEfFgG", t)) {
            if (!is_num(arg))
                throw_new(cls_TypeError, "must be real number, not %.40s", type_name(arg));
        } else if (t == 'c') {
            if (arg.t == T_STR) { buf_addn(&out, AS_STR(arg)->s, AS_STR(arg)->len); continue; }
        } else if (t != 's') {
            throw_new(cls_ValueError, "unsupported format character '%c'", t);
        }
        spec[sl++] = (char)t;
        spec[sl] = 0;
        if (t == 'd' && arg.t == T_BOOL) arg = mkint(arg.u.i);
        fmt_pct_mode = 1;
        res = format_value(arg, spec, sl);
        buf_addn(&out, AS_STR(res)->s, AS_STR(res)->len);
    }
    if (tup && argi < tup->n && used_single != 2)
        throw_new(cls_TypeError, "not all arguments converted during string formatting");
    if (!tup && used_single == 0 && argi == 0 && args.t != T_DICT)
        throw_new(cls_TypeError, "not all arguments converted during string formatting");
    return buf_value(&out);
}

/* ---- str.format ------------------------------------------------------- */
static Value format_template(Str *t, int nargs, Value *args, int nk, Value *kw, int depth)
{
    Buf out;
    long i = 0;
    int auto_idx = 0;
    buf_init(&out);
    if (depth > 3) throw_new(cls_ValueError, "Max string recursion exceeded");
    while (i < t->len) {
        int c = t->s[i];
        if (c == '{' && i + 1 < t->len && t->s[i + 1] == '{') { buf_addc(&out, '{');
            i += 2;
            continue;
        }
        if (c == '}' && i + 1 < t->len && t->s[i + 1] == '}') { buf_addc(&out, '}');
            i += 2;
            continue;
        }
        if (c == '}') throw_new(cls_ValueError, "Single '}' encountered in format string");
        if (c == '{') {
            long st = i + 1, j = st, fend, send = -1, sst = -1;
            int depthb = 0, conv = 0, k;
            Value v, sv;
            char name[200];
            long nl;
            for (; j < t->len; j++) {
                if (t->s[j] == '{') depthb++;
                else if (t->s[j] == '}') { if (depthb == 0) break; depthb--; }
            }
            if (j >= t->len) throw_new(cls_ValueError, "expected '}' before end of string");
            send = j;
            fend = st;
            while (fend < send && t->s[fend] != '!' && t->s[fend] != ':') fend++;
            nl = fend - st;
            if (nl > 190) nl = 190;
            memcpy(name, t->s + st, (size_t)nl);
            name[nl] = 0;
            if (fend < send && t->s[fend] == '!') { conv = t->s[fend + 1]; fend += 2; }
            if (fend < send && t->s[fend] == ':') sst = fend + 1;
            /* resolve field */
            {
                char *p = name;
                char first[200];
                int fl = 0;
                while (*p && *p != '.' && *p != '[') first[fl++] = *p++;
                first[fl] = 0;
                if (fl == 0) {
                    if (auto_idx >= nargs)
                        throw_new(cls_IndexError,
                        "Replacement index %d out of range for positional args tuple", auto_idx);
                    v = args[auto_idx++];
                } else if (ch_digit[(unsigned char)first[0]]) {
                    int ix = atoi(first);
                    if (ix >= nargs)
                        throw_new(cls_IndexError,
                        "Replacement index %d out of range for positional args tuple", ix);
                    v = args[ix];
                } else {
                    int found = 0;
                    v = NONE_V;
                    for (k = 0; k < nk; k++)
                        if (strcmp(AS_STR(kw[2 * k])->s, first) == 0) { v = kw[2 * k + 1];
                            found = 1;
                        }
                    if (!found) throw_key(mkcstr(first));
                }
                while (*p) {
                    if (*p == '.') {
                        char an[100];
                        int al = 0;
                        p++;
                        while (*p && *p != '.' && *p != '[' && al < 99) an[al++] = *p++;
                        an[al] = 0;
                        v = get_attr(v, intern_c(an));
                    } else if (*p == '[') {
                        char in[100];
                        int il = 0;
                        p++;
                        while (*p && *p != ']' && il < 99) in[il++] = *p++;
                        in[il] = 0;
                        if (*p == ']') p++;
                        if (ch_digit[(unsigned char)in[0]]) v = subscr(v, mkint(atol(in)));
                        else v = subscr(v, mkcstr(in));
                    } else break;
                }
            }
            if (conv == 'r' || conv == 'a') v = py_repr(v);
            else if (conv == 's') v = py_str(v);
            if (sst >= 0) {
                Value spec = mkstr(t->s + sst, send - sst);
                if (memchr(t->s + sst, '{', (size_t)(send - sst)))
                    spec = format_template(AS_STR(spec), nargs, args, nk, kw, depth + 1);
                sv = format_value(v, AS_STR(spec)->s, AS_STR(spec)->len);
            } else sv = format_value(v, "", 0);
            buf_addn(&out, AS_STR(sv)->s, AS_STR(sv)->len);
            i = send + 1;
            continue;
        }
        buf_addc(&out, c);
        i++;
    }
    return buf_value(&out);
}
