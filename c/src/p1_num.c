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
    bn_alloc(b, 8);
    if (v < 0) { b->sign = -1; u = 0UL - (unsigned long)v; }
    else { b->sign = 1; u = (unsigned long)v; }
    while (u > 0) { b->d[i++] = (long)(u % (unsigned long)BASE); u /= (unsigned long)BASE; }
    b->n = i;
    bn_trim(b);
}
static Value bn_to_value(BN *b);
/* a long outside +-SI_MAX as a big integer (see mkint) */
static Value big_from_long(long n)
{
    BN b;
    bn_from_long(&b, n);
    return bn_to_value(&b);
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
static int int_to_bin(Value v, unsigned long **out);
static double int_todouble(Value a)
{
    unsigned long *d;
    int n, i, bit, round_bit, sticky = 0;
    long bits, k;
    double m = 0.0;
    unsigned long top;
    if (a.t != T_BIG) return (double)a.u.i;
    /* correctly rounded (half to even) from the binary digits; no strtod involved */
    n = int_to_bin(a, &d);
    top = d[n - 1];
    bits = 16L * (n - 1);
    while (top) { bits++; top >>= 1; }
    if (bits > 1024) { free(d); throw_new(cls_OverflowError, "int too large to convert to float"); }
    /* the top 53 bits (or all of them) as an exact double, bit 'bits-1' downward */
    for (k = bits - 1; k >= 0 && k > bits - 54; k--) {
        bit = (int)((d[k / 16] >> (k % 16)) & 1UL);
        m = m * 2.0 + (double)bit;
    }
    k = bits - 54;                 /* highest bit not used: the round bit */
    if (k >= 0) {
        round_bit = (int)((d[k / 16] >> (k % 16)) & 1UL);
        for (i = 0; i < k / 16; i++) if (d[i]) sticky = 1;
        if (k / 16 < n && (d[k / 16] & ((1UL << (k % 16)) - 1UL))) sticky = 1;
        if (round_bit && (sticky || fmod(m, 2.0) == 1.0)) m += 1.0;
        free(d);
        m = ldexp(m, (int)(bits - 53));
    } else {
        free(d);
    }
    if (m > DBL_MAX) throw_new(cls_OverflowError, "int too large to convert to float");
    return AS_BIG(a)->sign < 0 ? -m : m;
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
/* digits of |v| in the given base (2..36), no prefix; '-' in front when negative */
static void int_str_base(Value v, int base, Buf *b)
{
    static const char dg[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *tmp;
    long n = 0, i, cap;
    int neg = 0;
    if (int_sign(v) < 0) { neg = 1; v = int_neg(v); }
    if (int_sign(v) == 0) { buf_addc(b, '0'); return; }
    if (v.t != T_BIG) {
        long x = v.u.i;
        char t2[72];
        while (x > 0) { t2[n++] = dg[x % base]; x /= base; }
        if (neg) buf_addc(b, '-');
        for (i = n - 1; i >= 0; i--) buf_addc(b, t2[i]);
        return;
    } else {
        BN x;
        bn_from_value(&x, v);
        cap = 14L * x.n + 8;           /* one limb has at most 14 binary digits */
        tmp = (char *)xmalloc((size_t)cap);
        while (x.n > 0 && n < cap) tmp[n++] = dg[mag_divsmall(&x, base)];
        free(x.d);
    }
    if (neg) buf_addc(b, '-');
    for (i = n - 1; i >= 0; i--) buf_addc(b, tmp[i]);
    free(tmp);
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
/* 1 and *out set when the integer fits a long (a big integer between SI_MAX and LONG_MAX does) */
static int int_fits_long(Value v, long *out)
{
    Big *g;
    unsigned long u = 0, lim = (unsigned long)LONG_MAX;
    int i;
    if (v.t != T_BIG) { *out = v.u.i; return 1; }
    g = AS_BIG(v);
    for (i = g->n - 1; i >= 0; i--) {
        if (u > (lim - (unsigned long)g->d[i]) / 10000UL) return 0;
        u = u * 10000UL + (unsigned long)g->d[i];
    }
    *out = g->sign < 0 ? -(long)u : (long)u;
    return 1;
}
/* ---- binary view of integers: bit operations, bit_length, exact float conversions ---- */
/* |v| as base 65536 digits, least significant first, in a fresh array (free() it); returns
   the digit count, 0 for zero.  The array always has room for one more digit. */
static int int_to_bin(Value v, unsigned long **out)
{
    BN x;
    unsigned long *d;
    int n = 0;
    bn_from_value(&x, v);
    d = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)(x.n + 3));
    while (x.n > 0) d[n++] = (unsigned long)mag_divsmall(&x, 65536L);
    free(x.d);
    *out = d;
    return n;
}
/* the integer with magnitude d[0..n) (base 65536) and the given sign */
static Value int_from_bin(const unsigned long *d, int n, int neg)
{
    BN r;
    int i, j;
    long c, t;
    bn_alloc(&r, 2 * n + 3);
    r.n = 0;
    for (i = n - 1; i >= 0; i--) {
        c = (long)d[i];
        for (j = 0; j < r.n; j++) {
            t = r.d[j] * 65536L + c;
            c = t / BASE;
            r.d[j] = t % BASE;
        }
        while (c) { r.d[r.n++] = c % BASE; c /= BASE; }
    }
    r.sign = neg ? -1 : 1;
    return bn_to_value(&r);
}
static long int_bitlen(Value v)
{
    unsigned long *d, top;
    long bits;
    int n;
    if (v.t != T_BIG) {
        unsigned long u = v.u.i < 0 ? 0UL - (unsigned long)v.u.i : (unsigned long)v.u.i;
        bits = 0;
        while (u) { bits++; u >>= 1; }
        return bits;
    }
    n = int_to_bin(v, &d);
    top = d[n - 1];
    bits = 16L * (n - 1);
    while (top) { bits++; top >>= 1; }
    free(d);
    return bits;
}
static long int_bitcount(Value v)
{
    unsigned long *d, w;
    long c = 0;
    int n, i;
    n = int_to_bin(v, &d);
    for (i = 0; i < n; i++)
        for (w = d[i]; w; w >>= 1) c += (long)(w & 1UL);
    free(d);
    return c;
}
/* two's complement digits (n of them, base 65536) of the number with magnitude d[0..have) */
static unsigned long *bin_twos(unsigned long *d, int have, int n, int neg)
{
    int i;
    d = (unsigned long *)xrealloc(d, sizeof(unsigned long) * (size_t)(n + 1));
    for (i = have; i < n; i++) d[i] = 0;
    if (neg) {
        for (i = 0; i < n; i++) {
            if (d[i]) { d[i]--; break; }
            d[i] = 0xFFFFUL;
        }
        for (i = 0; i < n; i++) d[i] = (~d[i]) & 0xFFFFUL;
    }
    return d;
}
/* a & b, a | b, a ^ b for any integers, with two's complement semantics (op: '&' '|' '^') */
static Value int_bitop(int op, Value a, Value b)
{
    unsigned long *x, *y;
    int nx, ny, n, i, na = int_sign(a) < 0, nb = int_sign(b) < 0, rneg;
    Value r;
    if (a.t != T_BIG && b.t != T_BIG) {
        long p = a.u.i, q = b.u.i;
        return mkint(op == '&' ? (p & q) : (op == '|' ? (p | q) : (p ^ q)));
    }
    nx = int_to_bin(a, &x);
    ny = int_to_bin(b, &y);
    n = (nx > ny ? nx : ny) + 1;
    x = bin_twos(x, nx, n, na);
    y = bin_twos(y, ny, n, nb);
    for (i = 0; i < n; i++) {
        if (op == '&') x[i] &= y[i];
        else if (op == '|') x[i] |= y[i];
        else x[i] ^= y[i];
    }
    rneg = (int)((x[n - 1] >> 15) & 1UL);
    if (rneg) {
        for (i = 0; i < n; i++) x[i] = (~x[i]) & 0xFFFFUL;
        for (i = 0; i < n; i++) {
            x[i]++;
            if (x[i] <= 0xFFFFUL) break;
            x[i] = 0;
        }
    }
    r = int_from_bin(x, n, rneg);
    free(x); free(y);
    return r;
}
static Value int_invert(Value a)
{
    return int_sub(int_neg(a), mkint(1));
}
/* 2**k */
static Value int_pow2(long k)
{
    return int_pow(mkint(2), k);
}
/* the integer value of an integral double (no NaN, no infinity) */
static Value int_from_double(double d)
{
    double t, f, mant;
    int e;
    Value r, hi;
    if (d != d) throw_new(cls_ValueError, "cannot convert float NaN to integer");
    if (d > DBL_MAX || d < -DBL_MAX)
        throw_new(cls_OverflowError, "cannot convert float infinity to integer");
    t = d < 0 ? ceil(d) : floor(d);
    if (t < (double)SI_MAX && t > -(double)SI_MAX) return mkint((long)t);  /* 32-bit long too */
    f = frexp(t < 0 ? -t : t, &e);      /* 0.5 <= f < 1, |t| = f * 2**e */
    mant = ldexp(f, 53);                /* an exact integer below 2**53 */
    e -= 53;
    if (e < 0) { mant = ldexp(mant, e); e = 0; }    /* exact: t is integral */
    hi = mkint((long)floor(mant / 67108864.0));    /* 2**26 */
    r = int_add(int_mul(hi, mkint(67108864L)),
                mkint((long)(mant - floor(mant / 67108864.0) * 67108864.0)));
    if (e > 0) r = int_mul(r, int_pow2(e));
    return t < 0 ? int_neg(r) : r;
}
/* exact comparison of an integer with a (non-NaN) double: -1, 0, 1 */
static int int_cmp_dbl(Value a, double d)
{
    double t;
    int c;
    if (d > DBL_MAX) return -1;
    if (d < -DBL_MAX) return 1;
    if (a.t != T_BIG) {
        double x = (double)a.u.i;
        if (x < 9007199254740992.0 && x > -9007199254740992.0)       /* exact below 2**53 */
            return x < d ? -1 : (x > d ? 1 : 0);
    }
    t = d < 0 ? ceil(d) : floor(d);
    c = int_cmp(a, int_from_double(t));
    if (c) return c;
    return d > t ? -1 : (d < t ? 1 : 0);
}
/* a / b with correct rounding for integers of any size; b != 0 */
static double int_truediv(Value a, Value b)
{
    double x, y, r;
    long la, lb, s;
    Value q, rem, A, B;
    int neg;
    if (a.t != T_BIG && b.t != T_BIG) {
        x = (double)a.u.i; y = (double)b.u.i;
        if (x < 9007199254740992.0 && x > -9007199254740992.0 &&
            y < 9007199254740992.0 && y > -9007199254740992.0) return x / y;
    }
    neg = (int_sign(a) < 0) != (int_sign(b) < 0);
    if (int_sign(a) == 0) return neg ? -0.0 : 0.0;
    A = int_sign(a) < 0 ? int_neg(a) : a;
    B = int_sign(b) < 0 ? int_neg(b) : b;
    la = int_bitlen(A); lb = int_bitlen(B);
    s = lb - la + 66;               /* the quotient gets 65 or 66 bits */
    if (s >= 0) A = int_mul(A, int_pow2(s));
    else B = int_mul(B, int_pow2(-s));
    int_divmod(A, B, &q, &rem);
    q = int_add(int_mul(q, mkint(2)), mkint(int_sign(rem) != 0));    /* sticky bit */
    x = int_todouble(q);
    r = ldexp(x, (int)(-s - 1));
    if (r > DBL_MAX) throw_new(cls_OverflowError, "integer division result too large for a float");
    return neg ? -r : r;
}
/* CPython's hash of an integer: the value modulo 2**61 - 1 (sign kept), -1 becomes -2 */
static Value int_hash_mod(Value v)
{
    Value m, r;
    int neg;
    if (v.t != T_BIG) return v.u.i == -1 ? mkint(-2) : mkint(v.u.i);
    neg = AS_BIG(v)->sign < 0;
    m = int_sub(int_pow2(61), mkint(1));
    r = int_mod(neg ? int_neg(v) : v, m);
    if (neg) r = int_neg(r);
    if (r.t == T_INT && r.u.i == -1) r = mkint(-2);
    return r;
}
/* CPython's hash of a float (finite: m * 2**e is reduced modulo 2**61 - 1, where 2 has the
   order 61); equal to the hash of the integer when the float is integral */
static Value float_hash_exact(double d)
{
    double f, mant;
    int e, r61, neg = 0;
    Value m, h;
    if (d != d) return mkint(0);
    if (d > DBL_MAX) return mkint(314159L);
    if (d < -DBL_MAX) return mkint(-314159L);
    if (d == floor(d)) {
        if (d < 1e9 && d > -1e9) return mkint(d == -1.0 ? -2L : (long)d);
        return int_hash_mod(int_from_double(d));
    }
    if (d < 0) { neg = 1; d = -d; }
    f = frexp(d, &e);
    mant = ldexp(f, 53);
    e -= 53;                              /* e < 0 here */
    r61 = (int)(((long)e % 61L + 61L) % 61L);
    m = int_from_double(mant);
    h = int_mod(int_mul(m, int_pow2(r61)), int_sub(int_pow2(61), mkint(1)));
    if (neg) h = int_neg(h);
    if (h.t == T_INT && h.u.i == -1) h = mkint(-2);
    return h;
}
/* hash(complex(re, im)): hash(re) + 1000003 * hash(im), wrapped to a signed 64 bit value */
static Value complex_hash_exact(double re, double im)
{
    Value h = float_hash_exact(re), m64, r;
    if (im == 0.0) return h;
    h = int_add(h, int_mul(mkint(1000003L), float_hash_exact(im)));
    m64 = int_pow2(64);
    r = int_mod(h, m64);
    if (int_cmp(r, int_pow2(63)) >= 0) r = int_sub(r, m64);
    if (r.t == T_INT && r.u.i == -1) r = mkint(-2);
    return r;
}
/* the hash CPython takes from the integer returned by a user __hash__: the value itself when it
   fits a signed 64 bit Py_hash_t (-1 becomes -2), else hash(int), i.e. modulo 2**61 - 1 */
static Value user_hash_val(Value r)
{
    if (r.t != T_BIG) return r.u.i == -1 ? mkint(-2) : mkint(r.u.i);
    if (int_cmp(r, int_neg(int_pow2(63))) >= 0 && int_cmp(r, int_pow2(63)) < 0) return r;
    return int_hash_mod(r);
}
/* the exact hash as an unsigned long for dictionaries (the low word of the Python hash) */
static unsigned long hash_fold(Value h)
{
    if (h.t != T_BIG) return (unsigned long)h.u.i;
    {
        Big *g = AS_BIG(h);
        unsigned long x = 0;
        int i;
        for (i = g->n - 1; i >= 0; i--) x = x * 10000UL + g->d[i];   /* exact on a 64 bit long */
        return g->sign < 0 ? 0UL - x : x;
    }
}
static unsigned long int_hash(Value v)
{
    return hash_fold(int_hash_mod(v));
}
static double fl_mod(double a, double b)
{
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    else if (r == 0) r = b < 0 ? -0.0 : 0.0;       /* the sign of the divisor, as in CPython */
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
        fd = a / b;
        fd = (fd < 0 || (fd == 0 && 1.0 / fd < 0)) ? -0.0 : 0.0;
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
        int dl, found = 0;
        sprintf(b, "%.*e", p - 1, d);
        if (strtod(b, NULL) == d) break;
        /* the shortest digits may be a neighbour of the nearest p digits (power of 2 edges) */
        for (dl = 0; dl < 2 && !found && p < 17; dl++) {
            char c2[48];
            int k = (int)(strchr(b, 'e') - b) - 1, carry = 1;
            strcpy(c2, b);
            for (; k >= 0 && carry; k--) {
                if (c2[k] == '.' || c2[k] == '-') continue;
                if (dl == 0) { if (c2[k] == '9') c2[k] = '0'; else { c2[k]++; carry = 0; } }
                else { if (c2[k] == '0') c2[k] = '9'; else { c2[k]--; carry = 0; } }
            }
            if (carry || c2[b[0] == '-'] == '0') continue;
            if (strtod(c2, NULL) == d) { strcpy(b, c2); found = 1; }
        }
        if (found) break;
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
    char *tmp, *end;
    long i = 0, e = n, k, m;
    int ok;
    while (i < e && ch_space[(unsigned char)s[i]]) i++;
    while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    if (e - i <= 0) return 0;
    tmp = (char *)xmalloc((size_t)(e - i) + 1);
    for (k = i, m = 0; k < e; k++) {
        if (s[k] == '_') {               /* only between two digits */
            if (k == i || k + 1 >= e || !ch_digit[(unsigned char)s[k - 1]] ||
                !ch_digit[(unsigned char)s[k + 1]]) { free(tmp); return 0; }
            continue;
        }
        tmp[m++] = (char)to_low[(unsigned char)s[k]];
    }
    tmp[m] = 0;
    {
        const char *t = tmp;
        int sg = 1;
        if (*t == '-') { sg = -1; t++; } else if (*t == '+') t++;
        if (strcmp(t, "inf") == 0 || strcmp(t, "infinity") == 0) {
            *out = sg * HUGE_VAL; free(tmp); return 1;
        }
        if (strcmp(t, "nan") == 0) { *out = HUGE_VAL - HUGE_VAL; free(tmp); return 1; }
        if (!(ch_digit[(unsigned char)*t] || *t == '.') || strchr(t, 'x') || strchr(t, 'n') ||
            strchr(t, 'i')) { free(tmp); return 0; }
    }
    *out = strtod(tmp, &end);
    ok = *end == 0;
    free(tmp);
    return ok;
}
