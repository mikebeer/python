/* =========================================================================
   p1_num.c   integers (small + big), floats, number <-> text
   ========================================================================= */
#define BASE 10000L

typedef struct BN { int sign; int n; long *d; } BN;

static int is_intv(Value v)
{
    return v.t == T_INT || v.t == T_BOOL || v.t == T_BIG;
}

static void bn_trim(BN *b)
{
    while (b->n > 0 && b->d[b->n - 1] == 0) b->n--;
    if (b->n == 0) b->sign = 0;
}
static void bn_alloc(BN *b, int n)
{
    b->d = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    b->n = n;
    b->sign = 1;
}
static void bn_from_long(BN *b, long v)
{
    unsigned long u;
    int i = 0;
    bn_alloc(b, 6);
    if (v < 0) { b->sign = -1; u = (unsigned long)(-v); }
    else { b->sign = 1; u = (unsigned long)v; }
    while (u > 0) { b->d[i++] = (long)(u % (unsigned long)BASE); u /= (unsigned long)BASE; }
    b->n = i;
    bn_trim(b);
}
static void bn_from_value(BN *b, Value v)
{
    if (v.t == T_BIG) {
        Big *g = AS_BIG(v);
        int i;
        bn_alloc(b, g->n);
        b->sign = g->sign;
        for (i = 0; i < g->n; i++) b->d[i] = g->d[i];
    } else {
        bn_from_long(b, v.u.i);
    }
}
static Value bn_to_value(BN *b)
{
    bn_trim(b);
    {
        unsigned long u = 0;
        int i, ok = 1;
        for (i = b->n - 1; i >= 0; i--) {
            if (u > (ULONG_MAX - 9999UL) / 10000UL) { ok = 0; break; }
            u = u * 10000UL + (unsigned long)b->d[i];
        }
        if (ok && u <= (unsigned long)SI_MAX) {
            Value r = mkint(b->sign < 0 ? -(long)u : (long)u);
            free(b->d);
            return r;
        }
    }
    {
        Big *g = (Big *)gc_alloc(sizeof(Big) + sizeof(unsigned short) * (size_t)b->n, T_BIG);
        int i;
        g->sign = b->sign;
        g->n = b->n;
        for (i = 0; i < b->n; i++) g->d[i] = (unsigned short)b->d[i];
        free(b->d);
        return mkobj(g, T_BIG);
    }
}
static int mag_cmp(const long *a, int na, const long *b, int nb)
{
    int i;
    if (na != nb) return na < nb ? -1 : 1;
    for (i = na - 1; i >= 0; i--)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
/* r = a + b magnitudes */
static void mag_add(BN *r, const BN *a, const BN *b)
{
    int n = (a->n > b->n ? a->n : b->n), i;
    long c = 0, s;
    bn_alloc(r, n + 1);
    for (i = 0; i < n; i++) {
        s = (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0) + c;
        if (s >= BASE) { s -= BASE; c = 1; } else c = 0;
        r->d[i] = s;
    }
    r->d[n] = c;
    bn_trim(r);
}
/* r = a - b magnitudes, requires |a| >= |b| */
static void mag_sub(BN *r, const BN *a, const BN *b)
{
    int i;
    long c = 0, s;
    bn_alloc(r, a->n);
    for (i = 0; i < a->n; i++) {
        s = a->d[i] - (i < b->n ? b->d[i] : 0) - c;
        if (s < 0) { s += BASE; c = 1; } else c = 0;
        r->d[i] = s;
    }
    bn_trim(r);
}
static void mag_mul(BN *r, const BN *a, const BN *b)
{
    int i, j;
    long c, t;
    bn_alloc(r, a->n + b->n + 1);
    for (i = 0; i < r->n; i++) r->d[i] = 0;
    for (i = 0; i < a->n; i++) {
        c = 0;
        for (j = 0; j < b->n; j++) {
            t = r->d[i + j] + a->d[i] * b->d[j] + c;
            c = t / BASE;
            r->d[i + j] = t % BASE;
        }
        j = i + b->n;
        while (c) { t = r->d[j] + c; c = t / BASE; r->d[j] = t % BASE; j++; }
    }
    bn_trim(r);
}
/* short division of magnitude by small s (< BASE*... up to 32767) */
static long mag_divsmall(BN *a, long s)
{
    int i;
    long rem = 0, t;
    for (i = a->n - 1; i >= 0; i--) {
        t = rem * BASE + a->d[i];
        a->d[i] = t / s;
        rem = t % s;
    }
    bn_trim(a);
    return rem;
}
/* Knuth algorithm D: q = a / b, r = a % b (magnitudes) */
static void mag_divmod(const BN *a, const BN *b, BN *q, BN *r)
{
    int m = a->n, n = b->n, i, j;
    long d, qhat, rhat, num, borrow, carry, p, sub, t;
    long *un, *vn;
    if (mag_cmp(a->d, a->n, b->d, b->n) < 0) {
        bn_alloc(q, 1); q->d[0] = 0; q->n = 0; q->sign = 0;
        bn_alloc(r, a->n);
        for (i = 0; i < a->n; i++) r->d[i] = a->d[i];
        bn_trim(r);
        return;
    }
    if (n == 1) {
        long rem = 0;
        bn_alloc(q, m);
        for (i = m - 1; i >= 0; i--) {
            t = rem * BASE + a->d[i];
            q->d[i] = t / b->d[0];
            rem = t % b->d[0];
        }
        bn_trim(q);
        bn_from_long(r, rem);
        return;
    }
    un = (long *)xmalloc(sizeof(long) * (size_t)(m + 1));
    vn = (long *)xmalloc(sizeof(long) * (size_t)n);
    d = BASE / (b->d[n - 1] + 1);
    carry = 0;
    for (i = 0; i < m; i++) { t = a->d[i] * d + carry; un[i] = t % BASE; carry = t / BASE; }
    un[m] = carry;
    carry = 0;
    for (i = 0; i < n; i++) { t = b->d[i] * d + carry; vn[i] = t % BASE; carry = t / BASE; }
    bn_alloc(q, m - n + 1);
    for (j = m - n; j >= 0; j--) {
        num = un[j + n] * BASE + un[j + n - 1];
        qhat = num / vn[n - 1];
        rhat = num % vn[n - 1];
        while (qhat >= BASE || qhat * vn[n - 2] > rhat * BASE + un[j + n - 2]) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= BASE) break;
        }
        borrow = 0; carry = 0;
        for (i = 0; i < n; i++) {
            p = qhat * vn[i] + carry;
            carry = p / BASE;
            sub = un[i + j] - (p % BASE) - borrow;
            if (sub < 0) { sub += BASE; borrow = 1; } else borrow = 0;
            un[i + j] = sub;
        }
        sub = un[j + n] - carry - borrow;
        if (sub < 0) { sub += BASE; borrow = 1; } else borrow = 0;
        un[j + n] = sub;
        if (borrow) {
            qhat--;
            carry = 0;
            for (i = 0; i < n; i++) {
                t = un[i + j] + vn[i] + carry;
                un[i + j] = t % BASE;
                carry = t / BASE;
            }
            un[j + n] = (un[j + n] + carry) % BASE;
        }
        q->d[j] = qhat;
    }
    bn_trim(q);
    bn_alloc(r, n);
    for (i = 0; i < n; i++) r->d[i] = un[i];
    bn_trim(r);
    mag_divsmall(r, d);
    free(un);
    free(vn);
}

static Value big_add(Value a, Value b, int sub)
{
    BN x, y, r;
    Value res;
    bn_from_value(&x, a);
    bn_from_value(&y, b);
    if (sub) y.sign = -y.sign;
    if (y.sign == 0) { free(y.d); free(x.d); return a; }
    if (x.sign == 0 || x.n == 0) {
        free(x.d);
        res = bn_to_value(&y);
        return res;
    }
    if (x.sign == y.sign) {
        mag_add(&r, &x, &y);
        r.sign = x.sign;
    } else {
        int c = mag_cmp(x.d, x.n, y.d, y.n);
        if (c == 0) { bn_alloc(&r, 1); r.d[0] = 0; r.n = 0; r.sign = 0; }
        else if (c > 0) { mag_sub(&r, &x, &y); r.sign = x.sign; }
        else { mag_sub(&r, &y, &x); r.sign = y.sign; }
    }
    free(x.d); free(y.d);
    return bn_to_value(&r);
}
static Value int_add(Value a, Value b)
{
    if (a.t != T_BIG && b.t != T_BIG) {
        long r = a.u.i + b.u.i;
        if (r <= SI_MAX && r >= -SI_MAX) return mkint(r);
    }
    return big_add(a, b, 0);
}
static Value int_sub(Value a, Value b)
{
    if (a.t != T_BIG && b.t != T_BIG) {
        long r = a.u.i - b.u.i;
        if (r <= SI_MAX && r >= -SI_MAX) return mkint(r);
    }
    return big_add(a, b, 1);
}
static Value int_mul(Value a, Value b)
{
    BN x, y, r;
    if (a.t != T_BIG && b.t != T_BIG &&
        a.u.i < SI_MULLIM && a.u.i > -SI_MULLIM && b.u.i < SI_MULLIM && b.u.i > -SI_MULLIM)
        return mkint(a.u.i * b.u.i);
    bn_from_value(&x, a);
    bn_from_value(&y, b);
    if (x.sign == 0 || y.sign == 0) { free(x.d); free(y.d); return mkint(0); }
    mag_mul(&r, &x, &y);
    r.sign = x.sign * y.sign;
    free(x.d); free(y.d);
    return bn_to_value(&r);
}
static Value int_neg(Value a)
{
    BN x;
    if (a.t != T_BIG) return mkint(-a.u.i);
    bn_from_value(&x, a);
    x.sign = -x.sign;
    return bn_to_value(&x);
}
static int int_sign(Value a)
{
    if (a.t == T_BIG) return AS_BIG(a)->sign;
    return a.u.i > 0 ? 1 : (a.u.i < 0 ? -1 : 0);
}
static int int_cmp(Value a, Value b)
{
    if (a.t != T_BIG && b.t != T_BIG)
        return a.u.i < b.u.i ? -1 : (a.u.i > b.u.i ? 1 : 0);
    {
        BN x, y;
        int c;
        bn_from_value(&x, a);
        bn_from_value(&y, b);
        if (x.sign != y.sign) c = x.sign < y.sign ? -1 : 1;
        else {
            c = mag_cmp(x.d, x.n, y.d, y.n);
            if (x.sign < 0) c = -c;
        }
        free(x.d); free(y.d);
        return c;
    }
}
/* floor division and modulo */
static void int_divmod(Value a, Value b, Value *q, Value *r)
{
    if (int_sign(b) == 0)
        throw_new(cls_ZeroDivisionError, "integer division or modulo by zero");
    if (a.t != T_BIG && b.t != T_BIG) {
        long x = a.u.i, y = b.u.i, qq = x / y, rr = x % y;
        if (rr != 0 && ((rr < 0) != (y < 0))) { qq--; rr += y; }
        *q = mkint(qq); *r = mkint(rr);
        return;
    }
    {
        BN x, y, qq, rr;
        int sx, sy;
        Value vq, vr;
        bn_from_value(&x, a);
        bn_from_value(&y, b);
        sx = x.sign; sy = y.sign;
        if (sx == 0) {
            free(x.d); free(y.d);
            *q = mkint(0); *r = mkint(0);
            return;
        }
        mag_divmod(&x, &y, &qq, &rr);
        if (sx == sy) {
            qq.sign = qq.n ? 1 : 0;
            rr.sign = rr.n ? sy : 0;
        } else if (rr.n == 0) {
            qq.sign = qq.n ? -1 : 0;
            rr.sign = 0;
        } else {
            BN one, t, nq, nr;
            bn_from_long(&one, 1);
            qq.sign = 1;
            mag_add(&nq, &qq, &one);
            nq.sign = -1;
            mag_sub(&nr, &y, &rr);
            nr.sign = sy;
            (void)t;
            free(one.d); free(qq.d); free(rr.d);
            qq = nq; rr = nr;
        }
        free(x.d); free(y.d);
        vq = bn_to_value(&qq);
        vr = bn_to_value(&rr);
        *q = vq; *r = vr;
    }
}
static Value int_floordiv(Value a, Value b)
{
    Value q, r;
    int_divmod(a, b, &q, &r);
    return q;
}
static Value int_mod(Value a, Value b)
{
    Value q, r;
    if (int_sign(b) == 0) throw_new(cls_ZeroDivisionError, "integer modulo by zero");
    int_divmod(a, b, &q, &r);
    return r;
}
static void int_str(Value v, Buf *b);
static double int_todouble(Value a)
{
    if (a.t != T_BIG) return (double)a.u.i;
    {
        Buf b;
        double d;
        buf_init(&b);
        int_str(a, &b);
        d = strtod(b.s, NULL);
        buf_free(&b);
        if (d > DBL_MAX || d < -DBL_MAX)
            throw_new(cls_OverflowError, "int too large to convert to float");
        return d;
    }
}
static Value int_pow(Value a, long e)
{
    Value r = mkint(1), base = a;
    while (e > 0) {
        if (e & 1) r = int_mul(r, base);
        e >>= 1;
        if (e) base = int_mul(base, base);
    }
    return r;
}
static void int_str(Value v, Buf *b)
{
    char t[40];
    if (v.t != T_BIG) {
        sprintf(t, "%ld", v.u.i);
        buf_add(b, t);
    } else {
        Big *g = AS_BIG(v);
        int i;
        if (g->sign < 0) buf_addc(b, '-');
        sprintf(t, "%d", (int)g->d[g->n - 1]);
        buf_add(b, t);
        for (i = g->n - 2; i >= 0; i--) {
            sprintf(t, "%04d", (int)g->d[i]);
            buf_add(b, t);
        }
    }
}
/* digits in base 2, 8, 16 (no prefix) */
static void int_str_base(Value v, int base, Buf *b)
{
    static const char dg[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[400];
    int n = 0, neg = 0, i;
    if (int_sign(v) < 0) { neg = 1; v = int_neg(v); }
    if (int_sign(v) == 0) { buf_addc(b, '0'); return; }
    if (v.t != T_BIG) {
        long x = v.u.i;
        while (x > 0 && n < 399) { tmp[n++] = dg[x % base]; x /= base; }
    } else {
        BN x;
        bn_from_value(&x, v);
        while (x.n > 0 && n < 399) tmp[n++] = dg[mag_divsmall(&x, base)];
        free(x.d);
    }
    if (neg) buf_addc(b, '-');
    for (i = n - 1; i >= 0; i--) buf_addc(b, tmp[i]);
}
static int digit_val(int c)
{
    const char *p;
    static const char lo[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static const char up[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (c == 0) return -1;
    p = strchr(lo, c);
    if (p) return (int)(p - lo);
    p = strchr(up, c);
    if (p) return (int)(p - up);
    return -1;
}
/* parse an integer from text; returns 0 on error */
static int parse_int(const char *s, long n, int base, Value *out)
{
    long i = 0, e = n;
    int neg = 0, d, any = 0, lastus = 0;
    Value acc = mkint(0), bv;
    while (i < e && ch_space[(unsigned char)s[i]]) i++;
    while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    if (i < e && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    if (i + 1 < e && s[i] == '0') {
        int c = s[i + 1];
        if ((c == 'x' || c == 'X') && (base == 16 || base == 0)) { base = 16; i += 2; }
        else if ((c == 'o' || c == 'O') && (base == 8 || base == 0)) { base = 8; i += 2; }
        else if ((c == 'b' || c == 'B') && (base == 2 || base == 0)) { base = 2; i += 2; }
    }
    if (base == 0) base = 10;
    bv = mkint(base);
    for (; i < e; i++) {
        if (s[i] == '_' && any && !lastus) { lastus = 1; continue; }
        d = digit_val((unsigned char)s[i]);
        if (d < 0 || d >= base) return 0;
        lastus = 0;
        any = 1;
        if (acc.t != T_BIG && acc.u.i < SI_MAX / 40 / 1)
            acc = mkint(acc.u.i * base + d);
        else
            acc = int_add(int_mul(acc, bv), mkint(d));
    }
    if (!any || lastus) return 0;
    *out = neg ? int_neg(acc) : acc;
    return 1;
}
static Value int_from_double(double d)
{
    char b[400];
    Value r;
    if (d != d) throw_new(cls_ValueError, "cannot convert float NaN to integer");
    if (d > DBL_MAX || d < -DBL_MAX)
        throw_new(cls_OverflowError, "cannot convert float infinity to integer");
    if (d < (double)SI_MAX && d > -(double)SI_MAX) return mkint((long)d);  /* 32-bit long too */
    sprintf(b, "%.0f", d);
    if (parse_int(b, (long)strlen(b), 10, &r)) return r;
    return mkint(0);
}
static unsigned long int_hash(Value v)
{
    if (v.t != T_BIG) return (unsigned long)(v.u.i < 0 ? -v.u.i : v.u.i) * 2654435761UL
                              + (v.u.i < 0 ? 1UL : 0UL);
    {
        Big *g = AS_BIG(v);
        unsigned long h = 7;
        int i;
        for (i = 0; i < g->n; i++) h = h * 31 + g->d[i];
        return h;
    }
}
static double fl_mod(double a, double b)
{
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}
static double fl_floordiv(double a, double b)
{
    double mod = fmod(a, b), div = (a - mod) / b, fd;
    if (mod != 0 && ((b < 0) != (mod < 0))) div -= 1.0;
    if (div != 0) {
        fd = floor(div);
        if (div - fd > 0.5) fd += 1.0;
    } else {
        fd = a / b < 0 ? -0.0 : 0.0;
    }
    return fd;
}
/* shortest round-trip repr of a float */
static void float_repr(double d, char *out)
{
    char b[48], digits[32];
    int p, nd = 0, exp10, neg = 0, decpt, i;
    char *q;
    if (d != d) { strcpy(out, "nan"); return; }
    if (d > DBL_MAX) { strcpy(out, "inf"); return; }
    if (d < -DBL_MAX) { strcpy(out, "-inf"); return; }
    if (d == 0) { strcpy(out, (1.0 / d < 0) ? "-0.0" : "0.0"); return; }
    for (p = 1; p <= 17; p++) {
        sprintf(b, "%.*e", p - 1, d);
        if (strtod(b, NULL) == d) break;
    }
    q = b;
    if (*q == '-') { neg = 1; q++; }
    while (*q && *q != 'e') {
        if (*q != '.') digits[nd++] = *q;
        q++;
    }
    digits[nd] = 0;
    exp10 = atoi(q + 1);
    while (nd > 1 && digits[nd - 1] == '0') digits[--nd] = 0;
    decpt = exp10 + 1;
    q = out;
    if (neg) *q++ = '-';
    if (decpt > 16 || decpt < -3) {
        *q++ = digits[0];
        if (nd > 1) { *q++ = '.'; for (i = 1; i < nd; i++) *q++ = digits[i]; }
        sprintf(q, "e%c%02d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
    } else if (decpt <= 0) {
        *q++ = '0'; *q++ = '.';
        for (i = 0; i < -decpt; i++) *q++ = '0';
        for (i = 0; i < nd; i++) *q++ = digits[i];
        *q = 0;
    } else if (decpt >= nd) {
        for (i = 0; i < nd; i++) *q++ = digits[i];
        for (i = 0; i < decpt - nd; i++) *q++ = '0';
        *q++ = '.'; *q++ = '0'; *q = 0;
    } else {
        for (i = 0; i < decpt; i++) *q++ = digits[i];
        *q++ = '.';
        for (i = decpt; i < nd; i++) *q++ = digits[i];
        *q = 0;
    }
}
static int parse_float(const char *s, long n, double *out)
{
    char tmp[400];
    char *end;
    long i = 0, e = n;
    int k;
    while (i < e && ch_space[(unsigned char)s[i]]) i++;
    while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    if (e - i <= 0 || e - i > 380) return 0;
    memcpy(tmp, s + i, (size_t)(e - i));
    tmp[e - i] = 0;
    for (k = 0; tmp[k]; k++) tmp[k] = (char)to_low[(unsigned char)tmp[k]];
    {
        const char *t = tmp;
        int sg = 1;
        if (*t == '-') { sg = -1; t++; } else if (*t == '+') t++;
        if (strcmp(t, "inf") == 0 || strcmp(t, "infinity") == 0) { *out = sg * HUGE_VAL; return 1; }
        if (strcmp(t, "nan") == 0) { *out = sg * HUGE_VAL - HUGE_VAL; return 1; }
        if (!(ch_digit[(unsigned char)*t] || *t == '.')) return 0;
        if (strchr(t, 'x') || strchr(t, 'n') || strchr(t, 'i')) return 0;
    }
    *out = strtod(tmp, &end);
    return *end == 0;
}
