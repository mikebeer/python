/* =========================================================================
   p7_pyrand.c   the random module with CPython's exact generator and algorithms: MT19937 seeded
                 by init_by_array (int / hash / SHA-512 of str, bytes), random(), getrandbits(),
                 _randbelow(), randrange, choice, shuffle, sample, choices and the real-valued
                 distributions of Lib/random.py (3.13), getstate / setstate, Random instances with
                 independent state (subclasses may override random / getrandbits / _randbelow) and
                 SystemRandom.  The MT19937 core and the u64 helpers come from p7_rnd.c.
   ========================================================================= */
static double d_lgamma(double x);
static double log2_d(double v);

/* ---- SHA-512 (needed for string and bytes seeds), built on the u64 operations ---------- */
static const unsigned long sha_k[80][2] = {
    { 0x428A2F98UL, 0xD728AE22UL },
    { 0x71374491UL, 0x23EF65CDUL },
    { 0xB5C0FBCFUL, 0xEC4D3B2FUL },
    { 0xE9B5DBA5UL, 0x8189DBBCUL },
    { 0x3956C25BUL, 0xF348B538UL },
    { 0x59F111F1UL, 0xB605D019UL },
    { 0x923F82A4UL, 0xAF194F9BUL },
    { 0xAB1C5ED5UL, 0xDA6D8118UL },
    { 0xD807AA98UL, 0xA3030242UL },
    { 0x12835B01UL, 0x45706FBEUL },
    { 0x243185BEUL, 0x4EE4B28CUL },
    { 0x550C7DC3UL, 0xD5FFB4E2UL },
    { 0x72BE5D74UL, 0xF27B896FUL },
    { 0x80DEB1FEUL, 0x3B1696B1UL },
    { 0x9BDC06A7UL, 0x25C71235UL },
    { 0xC19BF174UL, 0xCF692694UL },
    { 0xE49B69C1UL, 0x9EF14AD2UL },
    { 0xEFBE4786UL, 0x384F25E3UL },
    { 0x0FC19DC6UL, 0x8B8CD5B5UL },
    { 0x240CA1CCUL, 0x77AC9C65UL },
    { 0x2DE92C6FUL, 0x592B0275UL },
    { 0x4A7484AAUL, 0x6EA6E483UL },
    { 0x5CB0A9DCUL, 0xBD41FBD4UL },
    { 0x76F988DAUL, 0x831153B5UL },
    { 0x983E5152UL, 0xEE66DFABUL },
    { 0xA831C66DUL, 0x2DB43210UL },
    { 0xB00327C8UL, 0x98FB213FUL },
    { 0xBF597FC7UL, 0xBEEF0EE4UL },
    { 0xC6E00BF3UL, 0x3DA88FC2UL },
    { 0xD5A79147UL, 0x930AA725UL },
    { 0x06CA6351UL, 0xE003826FUL },
    { 0x14292967UL, 0x0A0E6E70UL },
    { 0x27B70A85UL, 0x46D22FFCUL },
    { 0x2E1B2138UL, 0x5C26C926UL },
    { 0x4D2C6DFCUL, 0x5AC42AEDUL },
    { 0x53380D13UL, 0x9D95B3DFUL },
    { 0x650A7354UL, 0x8BAF63DEUL },
    { 0x766A0ABBUL, 0x3C77B2A8UL },
    { 0x81C2C92EUL, 0x47EDAEE6UL },
    { 0x92722C85UL, 0x1482353BUL },
    { 0xA2BFE8A1UL, 0x4CF10364UL },
    { 0xA81A664BUL, 0xBC423001UL },
    { 0xC24B8B70UL, 0xD0F89791UL },
    { 0xC76C51A3UL, 0x0654BE30UL },
    { 0xD192E819UL, 0xD6EF5218UL },
    { 0xD6990624UL, 0x5565A910UL },
    { 0xF40E3585UL, 0x5771202AUL },
    { 0x106AA070UL, 0x32BBD1B8UL },
    { 0x19A4C116UL, 0xB8D2D0C8UL },
    { 0x1E376C08UL, 0x5141AB53UL },
    { 0x2748774CUL, 0xDF8EEB99UL },
    { 0x34B0BCB5UL, 0xE19B48A8UL },
    { 0x391C0CB3UL, 0xC5C95A63UL },
    { 0x4ED8AA4AUL, 0xE3418ACBUL },
    { 0x5B9CCA4FUL, 0x7763E373UL },
    { 0x682E6FF3UL, 0xD6B2B8A3UL },
    { 0x748F82EEUL, 0x5DEFB2FCUL },
    { 0x78A5636FUL, 0x43172F60UL },
    { 0x84C87814UL, 0xA1F0AB72UL },
    { 0x8CC70208UL, 0x1A6439ECUL },
    { 0x90BEFFFAUL, 0x23631E28UL },
    { 0xA4506CEBUL, 0xDE82BDE9UL },
    { 0xBEF9A3F7UL, 0xB2C67915UL },
    { 0xC67178F2UL, 0xE372532BUL },
    { 0xCA273ECEUL, 0xEA26619CUL },
    { 0xD186B8C7UL, 0x21C0C207UL },
    { 0xEADA7DD6UL, 0xCDE0EB1EUL },
    { 0xF57D4F7FUL, 0xEE6ED178UL },
    { 0x06F067AAUL, 0x72176FBAUL },
    { 0x0A637DC5UL, 0xA2C898A6UL },
    { 0x113F9804UL, 0xBEF90DAEUL },
    { 0x1B710B35UL, 0x131C471BUL },
    { 0x28DB77F5UL, 0x23047D84UL },
    { 0x32CAAB7BUL, 0x40C72493UL },
    { 0x3C9EBE0AUL, 0x15C9BEBCUL },
    { 0x431D67C4UL, 0x9C100D4CUL },
    { 0x4CC5D4BEUL, 0xCB3E42B6UL },
    { 0x597F299CUL, 0xFC657E2AUL },
    { 0x5FCB6FABUL, 0x3AD6FAECUL },
    { 0x6C44198CUL, 0x4A475817UL }
};
static const unsigned long sha_h0[8][2] = {
    { 0x6A09E667UL, 0xF3BCC908UL },
    { 0xBB67AE85UL, 0x84CAA73BUL },
    { 0x3C6EF372UL, 0xFE94F82BUL },
    { 0xA54FF53AUL, 0x5F1D36F1UL },
    { 0x510E527FUL, 0xADE682D1UL },
    { 0x9B05688CUL, 0x2B3E6C1FUL },
    { 0x1F83D9ABUL, 0xFB41BD6BUL },
    { 0x5BE0CD19UL, 0x137E2179UL }
};
static u64 sha_rotr(u64 a, int k) { return u64_or(u64_shr(a, k), u64_shl(a, 64 - k)); }
static u64 sha_not(u64 a) { return u64_xor(a, u64_max()); }
static void sha512_block(u64 *H, const unsigned char *p)
{
    u64 W[80], v[8], t1, t2, s0, s1;
    int t, i;
    for (t = 0; t < 16; t++) {
        unsigned long hi = 0, lo = 0;
        for (i = 0; i < 4; i++) hi = (hi << 8) | p[8 * t + i];
        for (i = 4; i < 8; i++) lo = (lo << 8) | p[8 * t + i];
        W[t] = u64_c(hi, lo);
    }
    for (t = 16; t < 80; t++) {
        s0 = u64_xor(u64_xor(sha_rotr(W[t - 15], 1), sha_rotr(W[t - 15], 8)), u64_shr(W[t - 15],
            7));
        s1 = u64_xor(u64_xor(sha_rotr(W[t - 2], 19), sha_rotr(W[t - 2], 61)), u64_shr(W[t - 2], 6));
        W[t] = u64_add(u64_add(W[t - 16], s0), u64_add(W[t - 7], s1));
    }
    for (i = 0; i < 8; i++) v[i] = H[i];
    for (t = 0; t < 80; t++) {
        u64 e = v[4], a = v[0], ch, maj, S1, S0;
        S1 = u64_xor(u64_xor(sha_rotr(e, 14), sha_rotr(e, 18)), sha_rotr(e, 41));
        ch = u64_xor(u64_and(e, v[5]), u64_and(sha_not(e), v[6]));
        t1 = u64_add(u64_add(v[7], S1), u64_add(ch, u64_add(u64_c(sha_k[t][0], sha_k[t][1]),
            W[t])));
        S0 = u64_xor(u64_xor(sha_rotr(a, 28), sha_rotr(a, 34)), sha_rotr(a, 39));
        maj = u64_xor(u64_xor(u64_and(a, v[1]), u64_and(a, v[2])), u64_and(v[1], v[2]));
        t2 = u64_add(S0, maj);
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4];
        v[4] = u64_add(v[3], t1);
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0];
        v[0] = u64_add(t1, t2);
    }
    for (i = 0; i < 8; i++) H[i] = u64_add(H[i], v[i]);
}
/* digest[64] = SHA-512 of msg[0..len) */
static void sha512(const unsigned char *msg, long len, unsigned char *digest)
{
    u64 H[8];
    long total = ((len + 16) / 128 + 1) * 128, off, bits_hi, i;
    unsigned char *buf = (unsigned char *)xmalloc((size_t)total);
    unsigned long bl = (unsigned long)len;
    for (i = 0; i < 8; i++) H[i] = u64_c(sha_h0[i][0], sha_h0[i][1]);
    memset(buf, 0, (size_t)total);
    if (len > 0) memcpy(buf, msg, (size_t)len);
    buf[len] = 0x80;
    bits_hi = (long)(bl >> 29);                          /* bits 61.. of the length * 8 */
    buf[total - 8] = (unsigned char)((bits_hi >> 24) & 0xFF);
    buf[total - 7] = (unsigned char)((bits_hi >> 16) & 0xFF);
    buf[total - 6] = (unsigned char)((bits_hi >> 8) & 0xFF);
    buf[total - 5] = (unsigned char)(bits_hi & 0xFF);
    {
        unsigned long lo = (bl << 3) & U32M;             /* low 32 bits of the bit count */
        buf[total - 4] = (unsigned char)((lo >> 24) & 0xFF);
        buf[total - 3] = (unsigned char)((lo >> 16) & 0xFF);
        buf[total - 2] = (unsigned char)((lo >> 8) & 0xFF);
        buf[total - 1] = (unsigned char)(lo & 0xFF);
    }
    for (off = 0; off < total; off += 128) sha512_block(H, buf + off);
    for (i = 0; i < 8; i++) {
        unsigned long hi = u64_hi(H[i]), lo = u64_lo(H[i]);
        int k;
        for (k = 0; k < 4; k++) {
            digest[8 * i + k] = (unsigned char)((hi >> (24 - 8 * k)) & 0xFF);
            digest[8 * i + 4 + k] = (unsigned char)((lo >> (24 - 8 * k)) & 0xFF);
        }
    }
    free(buf);
}

/* ---- instances, seeding, state ---------------------------------------------------------- */
static Class *cls_pyrand, *cls_sysrand;
static Value pr_inst;                    /* the instance behind the module level functions */
static Value bi_pr_random, bi_pr_getrandbits, bi_pr_randbelow;
static Str *S_pr_s, *S_pr_gauss, *S_pr_random, *S_pr_getrandbits, *S_pr_randbelow, *S_pr_seed;

/* an unsigned 32-bit word as a Python int (long may be 32 bits) */
static Value u32v(unsigned long w)
{
    unsigned long d[2];
    w &= U32M;
    if (w <= 0x7FFFFFFFUL) return mkint((long)w);
    d[0] = w & 0xFFFFUL; d[1] = w >> 16;
    return int_from_bin(d, 2, 0);
}
static unsigned long v_u32(Value v)
{
    unsigned long *d, r;
    int n;
    if (v.t == T_BOOL) return (unsigned long)v.u.i;
    if (v.t == T_INT && v.u.i >= 0 && (unsigned long)v.u.i <= U32M) return (unsigned long)v.u.i;
    if (v.t != T_INT && v.t != T_BIG) throw_new(cls_TypeError, "state values must be integers");
    if (int_sign(v) < 0) throw_new(cls_ValueError, "invalid state");
    n = int_to_bin(v, &d);
    if (n > 2) { free(d); throw_new(cls_ValueError, "invalid state"); }
    r = (n > 0 ? d[0] : 0UL) | (n > 1 ? d[1] << 16 : 0UL);
    free(d);
    return r;
}
static void pr_seed_urandom(RG *g)
{
    unsigned char b[MT_N * 4];
    unsigned long key[MT_N];
    int i;
    host_urandom(b, MT_N * 4);
    for (i = 0; i < MT_N; i++)
        key[i] = ((unsigned long)b[4 * i] << 24) | ((unsigned long)b[4 * i + 1] << 16) |
            ((unsigned long)b[4 * i + 2] << 8) | (unsigned long)b[4 * i + 3];
    mt_init_array(g->mt, key, MT_N);
    g->mpos = MT_N;
}
static RG *pr_rg(Value self)
{
    Value *sv;
    if (self.t != T_INST) throw_new(cls_TypeError, "descriptor requires a 'random.Random' object");
    sv = amap_get(&AS_INST(self)->a, S_pr_s);
    if (!sv) {
        Value h;
        VPUSH(self);
        h = rg_holder();
        amap_set(&AS_INST(self)->a, S_pr_s, h);
        vsp--;
        {
            RG *g = (RG *)AS_ND(h)->iv;
            g->kind = 0;
            pr_seed_urandom(g);
            return g;
        }
    }
    return (RG *)AS_ND(*sv)->iv;
}
/* the key words (little endian, 32 bit) of |n| given as base 65536 digits */
static long key_from_digits(const unsigned long *d, long nd, unsigned long *key)
{
    long i, kw;
    while (nd > 0 && d[nd - 1] == 0) nd--;
    kw = nd == 0 ? 1 : (nd + 1) / 2;
    for (i = 0; i < kw; i++)
        key[i] = (2 * i < nd ? d[2 * i] : 0UL) | (2 * i + 1 < nd ? d[2 * i + 1] << 16 : 0UL);
    return kw;
}
static void seed_digits(RG *g, const unsigned long *d, long nd)
{
    unsigned long *key = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)(nd / 2 + 2));
    long kw = key_from_digits(d, nd, key);
    mt_init_array(g->mt, key, kw);
    g->mpos = MT_N;
    free(key);
}
static void seed_int(RG *g, Value n)
{
    unsigned long *d;
    int nd = int_to_bin(n, &d);
    seed_digits(g, d, nd);
    free(d);
}
/* int.from_bytes(a + sha512(a).digest()) as the seed */
static void seed_bytes_v2(RG *g, const unsigned char *a, long len)
{
    unsigned char dg[64];
    unsigned char *all = (unsigned char *)xmalloc((size_t)(len + 64));
    unsigned long *d;
    long total = len + 64, i;
    sha512(a, len, dg);
    if (len > 0) memcpy(all, a, (size_t)len);
    memcpy(all + len, dg, 64);
    d = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)(total / 2 + 2));
    for (i = 0; 2 * i < total; i++) {
        unsigned long lo = all[total - 1 - 2 * i];
        unsigned long hi = 2 * i + 1 < total ? all[total - 2 - 2 * i] : 0UL;
        d[i] = lo | (hi << 8);
    }
    seed_digits(g, d, (total + 1) / 2);
    free(d);
    free(all);
}
/* version 1 string seeds: the old hash, as an unsigned 64-bit number */
static void seed_str_v1(RG *g, const unsigned char *s, long len)
{
    u64 x = u64_ul(0UL), mask = u64_max(), mult = u64_ul(1000003UL);
    long i = 0, cnt = 0;
    unsigned long first = 0;
    unsigned long *cps = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)(len + 1));
    unsigned long d[5];
    while (i < len) {                                /* UTF-8 decode */
        unsigned long c = s[i];
        int extra = 0;
        if (c >= 0xF0) { c &= 7; extra = 3; }
        else if (c >= 0xE0) { c &= 15; extra = 2; }
        else if (c >= 0xC0) { c &= 31; extra = 1; }
        i++;
        while (extra-- > 0 && i < len) c = (c << 6) | (s[i++] & 63);
        cps[cnt++] = c;
    }
    if (cnt > 0) first = cps[0];
    x = u64_shl(u64_ul(first), 7);
    for (i = 0; i < cnt; i++) x = u64_and(u64_xor(u64_mul(mult, x), u64_ul(cps[i])), mask);
    x = u64_xor(x, u64_ul((unsigned long)cnt));
    free(cps);
    d[0] = u64_lo(x) & 0xFFFFUL; d[1] = u64_lo(x) >> 16;
    d[2] = u64_hi(x) & 0xFFFFUL; d[3] = u64_hi(x) >> 16;
    seed_digits(g, d, 4);
}
static void pr_seed(RG *g, Value a, int version)
{
    Value av = a;
    if (av.t == T_INST && AS_INST(av)->pay.t != T_UNSET) av = AS_INST(av)->pay;
    if (av.t == T_UNSET || av.t == T_NONE) { pr_seed_urandom(g); return; }
    if (version == 1 && av.t == T_STR) { seed_str_v1(g, (const unsigned char *)AS_STR(av)->s,
        AS_STR(av)->len); return; }
    if (version == 1 && av.t == T_BYTES) {
        /* latin-1 decoded: each byte is one character */
        Str *s = AS_STR(av);
        unsigned long *cp = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)(s->len + 1));
        u64 x = u64_shl(u64_ul(s->len > 0 ? (unsigned char)s->s[0] : 0UL), 7), mask = u64_max();
        unsigned long d[4];
        long i;
        for (i = 0; i < s->len; i++) cp[i] = (unsigned char)s->s[i];
        for (i = 0; i < s->len; i++)
            x = u64_and(u64_xor(u64_mul(u64_ul(1000003UL), x), u64_ul(cp[i])), mask);
        x = u64_xor(x, u64_ul((unsigned long)s->len));
        free(cp);
        d[0] = u64_lo(x) & 0xFFFFUL; d[1] = u64_lo(x) >> 16;
        d[2] = u64_hi(x) & 0xFFFFUL; d[3] = u64_hi(x) >> 16;
        seed_digits(g, d, 4);
        return;
    }
    if (av.t == T_STR || av.t == T_BYTES) {
        seed_bytes_v2(g, (const unsigned char *)AS_STR(av)->s, AS_STR(av)->len);
        return;
    }
    if (av.t == T_BARR) { seed_bytes_v2(g, (const unsigned char *)AS_BARR(av)->s, AS_BARR(av)->n);
    return; }
    if (av.t == T_INT || av.t == T_BIG) { seed_int(g, av); return; }
    if (av.t == T_BOOL) { seed_int(g, mkint(av.u.i)); return; }
    if (av.t == T_FLOAT) {
        Value h = float_hash_exact(av.u.d);          /* the hash as an unsigned 64-bit number */
        if (int_sign(h) < 0) h = int_add(h, int_pow2(64));
        seed_int(g, h);
        return;
    }
    throw_new(cls_TypeError,
        "The only supported seed types are:\nNone, int, float, str, bytes, and bytearray.");
}

/* ---- the three primitives, dispatched through the instance when a subclass overrides them -- */
typedef struct Rn {
    Value self;
    RG *g;
    int fast;            /* random / getrandbits / _randbelow are the native ones */
    Value rnd;           /* bound self.random, when not fast */
} Rn;
static int pr_is_native(Value self, Str *nm, Value native)
{
    Value *f = class_find(AS_INST(self)->cls, nm);
    return f && f->t == T_BUILTIN && f->u.o == native.u.o;
}
static void rn_init(Rn *r, Value self)
{
    if (self.t != T_INST) throw_new(cls_TypeError, "descriptor requires a 'random.Random' object");
    r->self = self;
    r->fast = AS_INST(self)->cls == cls_pyrand ||
        (pr_is_native(self, S_pr_random, bi_pr_random) &&
         pr_is_native(self, S_pr_getrandbits, bi_pr_getrandbits) &&
         pr_is_native(self, S_pr_randbelow, bi_pr_randbelow));
    r->g = r->fast || pr_is_native(self, S_pr_random, bi_pr_random) ||
        pr_is_native(self, S_pr_getrandbits, bi_pr_getrandbits) ? pr_rg(self) : NULL;
    r->rnd = UNSET_V;
}
static double rr_random(Rn *r)
{
    Value v;
    if (r->g && pr_is_native(r->self, S_pr_random, bi_pr_random)) return rg_double(r->g);
    if (r->rnd.t == T_UNSET) {
        r->rnd = get_attr(r->self, S_pr_random);
        VPUSH(r->rnd);
    }
    v = call0(r->rnd);
    if (v.t != T_FLOAT && v.t != T_INT && v.t != T_BIG && v.t != T_BOOL)
        throw_new(cls_TypeError, "random() must return a number, not '%.40s'", type_name(v));
    return to_dbl(v);
}
static double rg_rand(Rn *r) { return r->fast ? rg_double(r->g) : rr_random(r); }

/* k random bits as an integer, the way _random.Random.getrandbits builds them */
static Value gb_value(RG *g, long k)
{
    long words, i, nd;
    unsigned long *d, w;
    Value v;
    if (k < 0) throw_new(cls_ValueError, "number of bits must be non-negative");
    if (k == 0) return mkint(0);
    if (k <= 32) {
        w = mt_next(g) >> (32 - k);
        return u32v(w);
    }
    words = (k - 1) / 32 + 1;
    nd = 2 * words;
    d = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)nd);
    for (i = 0; i < words; i++) {
        w = mt_next(g);
        if (k < 32) w >>= (32 - k);
        d[2 * i] = w & 0xFFFFUL;
        d[2 * i + 1] = w >> 16;
        k -= 32;
    }
    v = int_from_bin(d, (int)nd, 0);
    free(d);
    return v;
}
static Value rr_getrandbits(Rn *r, long k)
{
    Value f, v;
    if (r->g && pr_is_native(r->self, S_pr_getrandbits, bi_pr_getrandbits)) return gb_value(r->g,
        k);
    f = get_attr(r->self, S_pr_getrandbits);
    v = call1(f, mkint(k));
    if (v.t != T_INT && v.t != T_BIG) throw_new(cls_TypeError, "getrandbits() must return an int");
    return v;
}
/* Random._randbelow_without_getrandbits */
static Value below_without(Rn *r, Value nv)
{
    double maxsize = 9007199254740992.0, rem, limit, x;
    if (int_cmp(nv, int_pow2(53)) >= 0)
        return int_from_double(floor(rr_random(r) * to_dbl(nv)));
    rem = fmod(maxsize, to_dbl(nv));
    limit = (maxsize - rem) / maxsize;
    x = rr_random(r);
    while (x >= limit) x = rr_random(r);
    return int_mod(int_from_double(floor(x * maxsize)), nv);
}
/* _randbelow_with_getrandbits */
static Value below_with(Rn *r, Value nv)
{
    long k = int_bitlen(nv);
    Value x = rr_getrandbits(r, k);
    while (int_cmp(x, nv) >= 0) x = rr_getrandbits(r, k);
    return x;
}
/* which algorithm Random.__init_subclass__ picks for this class: 1 = getrandbits, 0 = random only,
   -1 = a user _randbelow */
static int below_mode(Value self)
{
    Class *c = AS_INST(self)->cls, *k;
    int i = 0;
    for (;;) {
        if (c->mro) { if (i >= c->nmro) break; k = c->mro[i++]; }
        else { k = c; c = c->base; if (!k) break; }
        if (k == cls_pyrand) return 1;
        if (amap_get(&k->ns, S_pr_randbelow)) return -1;
        if (amap_get(&k->ns, S_pr_getrandbits)) return 1;
        if (amap_get(&k->ns, S_pr_random)) return 0;
        if (!c) break;
    }
    return 1;
}
static Value rr_below(Rn *r, Value nv)
{
    long n;
    if (r->fast) {
        RG *g = r->g;
        if (nv.t == T_INT) {
            n = nv.u.i;
            if (n <= 0) throw_new(cls_ValueError, "empty range for randrange()");
            {
                long k = int_bitlen(nv);
                if (k <= 32) {
                    unsigned long x = mt_next(g) >> (32 - k);
                    while (x >= (unsigned long)n) x = mt_next(g) >> (32 - k);
                    return mkint((long)x);
                } else {
                    u64 lim = u64_l(n), x;
                    for (;;) {
                        unsigned long lo = mt_next(g), hi = mt_next(g) >> (32 - (k - 32));
                        x = u64_c(hi, lo);
                        if (u64_lt(x, lim)) return mkint(u64_tol(x));
                    }
                }
            }
        }
        if (int_sign(nv) <= 0) throw_new(cls_ValueError, "empty range for randrange()");
        return below_with(r, nv);
    }
    {
        int mode = below_mode(r->self);
        if (mode < 0) {
            Value *f = class_find(AS_INST(r->self)->cls, S_pr_randbelow);
            if (f && !(f->t == T_BUILTIN && f->u.o == bi_pr_randbelow.u.o))
                return call1(get_attr(r->self, S_pr_randbelow), nv);
        }
        if (int_sign(nv) <= 0) throw_new(cls_ValueError, "empty range for randrange()");
        return mode == 0 ? below_without(r, nv) : below_with(r, nv);
    }
}

/* ---- argument and arithmetic helpers ---------------------------------------------------- */
#define HAS(v) ((v).t != T_UNSET)
#define NOARG(v) ((v).t == T_UNSET || (v).t == T_NONE)
static int is_intlike(Value v)
{
    return v.t == T_INT || v.t == T_BIG || v.t == T_BOOL ||
        (v.t == T_INST && (AS_INST(v)->pay.t == T_INT || AS_INST(v)->pay.t == T_BIG));
}
/* operator.index(v) with CPython's message */
static Value idx_of(Value v)
{
    if (v.t == T_INST && AS_INST(v)->pay.t != T_INT && AS_INST(v)->pay.t != T_BIG &&
        !class_find(AS_INST(v)->cls, S_index))
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
            type_name(v));
    if (v.t != T_INT && v.t != T_BIG && v.t != T_BOOL && v.t != T_INST)
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
            type_name(v));
    return need_intv(v, "value");
}
static double dnum(Value v)
{
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (v.t != T_INT && v.t != T_BIG && v.t != T_BOOL && v.t != T_FLOAT)
        throw_new(cls_TypeError, "must be real number, not %.40s", type_name(v));
    return to_dbl(v);
}
static double fdiv0(double a, double b)
{
    if (b == 0.0) throw_new(cls_ZeroDivisionError, "float division by zero");
    return a / b;
}
static double fpow(double x, double y)
{
    double r;
    if (x == 0.0 && y < 0.0) throw_new(cls_ZeroDivisionError,
        "0.0 cannot be raised to a negative power");
    r = pow(x, y);
    if ((r == HUGE_VAL || r == -HUGE_VAL) && x - x == 0.0 && y - y == 0.0)
        throw_new(cls_OverflowError, "(34, 'Numerical result out of range')");
    return r;
}
static double flog(double x)
{
    if (x <= 0.0) throw_new(cls_ValueError, "math domain error");
    return log(x);
}
static double fmodpy(double a, double b)
{
    double m;
    if (b == 0.0) throw_new(cls_ZeroDivisionError, "float modulo by zero");
    m = fmod(a, b);
    if (m != 0.0) { if ((b < 0.0) != (m < 0.0)) m += b; }
    else m = b < 0.0 ? -0.0 : 0.0;
    return m;
}
/* a - b the way Python subtracts two numbers: exactly for ints, then converted */
static double dsub(Value a, Value b)
{
    if (is_intlike(a) && is_intlike(b)) return to_dbl(int_sub(idx_of(a), idx_of(b)));
    return dnum(a) - dnum(b);
}
static Value rarg_n(Args *x, int off, int pos, const char *nm)
{
    if (pos < x->n - off) return x->a[off + pos];
    return kwget(x, nm);
}
#define ARG(i, nm) rarg_n(x, off, (i), (nm))
static Value need_arg(Args *x, int off, int pos, const char *nm, const char *fname)
{
    Value v = ARG(pos, nm);
    if (!HAS(v)) throw_new(cls_TypeError, "%s() missing required argument: '%s'", fname, nm);
    return v;
}
#define ARGD(var, i, nm, fname) var = dnum(need_arg(x, off, (i), (nm), (fname)))

/* ---- seed / state / primitives ----------------------------------------------------------- */
static void set_gauss_next(Value self, Value v) { amap_set(&AS_INST(self)->a, S_pr_gauss, v); }
static Value get_gauss_next(Value self)
{
    Value *p = amap_get(&AS_INST(self)->a, S_pr_gauss);
    return p ? *p : NONE_V;
}
static Value pyf_seed(Value self, Args *x, int off)
{
    Value a = ARG(0, "a"), ver = ARG(1, "version");
    int version = 2;
    RG *g;
    if (HAS(ver)) version = (int)need_long(ver, "version");
    g = pr_rg(self);
    pr_seed(g, a, version);
    set_gauss_next(self, NONE_V);
    return NONE_V;
}
static Value pyf_getstate(Value self, Args *x, int off)
{
    RG *g = pr_rg(self);
    List *t = tuple_new(MT_N + 1);
    Value parts[3];
    int i;
    (void)x; (void)off;
    for (i = 0; i < MT_N; i++) t->a[i] = u32v(g->mt[i]);
    t->a[MT_N] = mkint((long)g->mpos);
    parts[0] = mkint(3);
    parts[1] = mkobj(t, T_TUPLE);
    parts[2] = get_gauss_next(self);
    return mktuple_from(parts, 3);
}
static Value pyf_setstate(Value self, Args *x, int off)
{
    Value st = need_arg(x, off, 0, "state", "setstate"), ver, internal;
    RG *g = pr_rg(self);
    long version, i;
    unsigned long w[MT_N];
    List *t;
    if (st.t != T_TUPLE && st.t != T_LIST)
        throw_new(cls_TypeError, "'%.40s' object is not subscriptable", type_name(st));
    if (AS_LIST(st)->n < 1) throw_new(cls_IndexError, "tuple index out of range");
    ver = AS_LIST(st)->a[0];
    version = is_intlike(ver) ? need_long(ver, "version") : -1;
    if (version != 3 && version != 2)
        throw_new(cls_ValueError, "state with version %s passed to Random.setstate() of version 3",
                  AS_STR(py_str(ver))->s);
    if (AS_LIST(st)->n != 3)
        throw_new(cls_ValueError, AS_LIST(st)->n > 3 ? "too many values to unpack (expected 3)" :
                  "not enough values to unpack (expected 3, got %d)", (int)AS_LIST(st)->n);
    internal = AS_LIST(st)->a[1];
    if (internal.t != T_TUPLE) throw_new(cls_TypeError, "state vector must be a tuple");
    t = AS_LIST(internal);
    if (t->n != MT_N + 1) throw_new(cls_ValueError, "state vector is the wrong size");
    for (i = 0; i < MT_N; i++) {
        Value e = t->a[i];
        if (version == 2 && is_intlike(e)) {
            Value m = int_mod(idx_of(e), int_pow2(32));
            w[i] = v_u32(m);
        } else w[i] = v_u32(e);
    }
    {
        Value iv = t->a[MT_N];
        long idx;
        if (!is_intlike(iv)) throw_new(cls_TypeError, "state vector items must be integers");
        idx = need_long(iv, "index");
        if (idx < 0 || idx > MT_N) throw_new(cls_ValueError, "invalid state");
        for (i = 0; i < MT_N; i++) g->mt[i] = w[i];
        g->mpos = (int)idx;
    }
    set_gauss_next(self, AS_LIST(st)->a[2]);
    return NONE_V;
}
static Value pyf_random(Value self, Args *x, int off)
{
    (void)x; (void)off;
    return mkfloat(rg_double(pr_rg(self)));
}
static Value pyf_getrandbits(Value self, Args *x, int off)
{
    Value kv = need_arg(x, off, 0, "k", "getrandbits");
    return gb_value(pr_rg(self), need_long(idx_of(kv), "k"));
}
static Value pyf_randbytes(Value self, Args *x, int off)
{
    Rn r;
    Value nv = need_arg(x, off, 0, "n", "randbytes");
    long n = need_long(idx_of(nv), "n"), i;
    Value bits;
    unsigned char *buf;
    Value res;
    rn_init(&r, self);
    if (n < 0) throw_new(cls_ValueError, "number of bits must be non-negative");
    if (r.g && pr_is_native(self, S_pr_getrandbits, bi_pr_getrandbits)) {
        long k = n * 8, words, j;
        RG *g = r.g;
        buf = (unsigned char *)xmalloc((size_t)(n + 8));
        words = k == 0 ? 0 : (k - 1) / 32 + 1;
        for (i = 0; i < words; i++) {
            unsigned long w = mt_next(g);
            if (k < 32) w >>= (32 - k);
            for (j = 0; j < 4 &&
                4 * i + j < n; j++) buf[4 * i + j] = (unsigned char)((w >> (8 * j)) & 0xFF);
            k -= 32;
        }
        res = mkbytes((const char *)buf, n);
        free(buf);
        return res;
    }
    bits = rr_getrandbits(&r, n * 8);
    {
        unsigned long *d;
        int nd = int_to_bin(bits, &d);
        buf = (unsigned char *)xmalloc((size_t)(n + 2));
        memset(buf, 0, (size_t)(n + 2));
        for (i = 0; i < nd && 2 * i < n; i++) {
            buf[2 * i] = (unsigned char)(d[i] & 0xFF);
            if (2 * i + 1 < n) buf[2 * i + 1] = (unsigned char)((d[i] >> 8) & 0xFF);
        }
        free(d);
        res = mkbytes((const char *)buf, n);
        free(buf);
        return res;
    }
}
static Value pyf_randbelow(Value self, Args *x, int off)
{
    Rn r;
    Value nv = idx_of(need_arg(x, off, 0, "n", "_randbelow"));
    rn_init(&r, self);
    if (int_sign(nv) <= 0) throw_new(cls_ValueError, "empty range for randrange()");
    r.fast = r.g != NULL && pr_is_native(self, S_pr_getrandbits, bi_pr_getrandbits);
    if (r.fast) return rr_below(&r, nv);
    return below_with(&r, nv);
}

/* ---- integers --------------------------------------------------------------------------- */
#define SMALLI(v) ((v).t == T_INT && (v).u.i > -1000000000L && (v).u.i < 1000000000L)
static Value pyf_randrange(Value self, Args *x, int off)
{
    Rn r;
    Value start = need_arg(x, off, 0, "start", "randrange"), stop = ARG(1, "stop"),
        step = ARG(2, "step"), istart, istop, istep, width, n = NONE_V;
    rn_init(&r, self);
    istart = idx_of(start);
    if (NOARG(stop)) {
        if (HAS(step) && !(step.t == T_INT && step.u.i == 1))
            throw_new(cls_TypeError, "Missing a non-None stop argument");
        if (int_sign(istart) > 0) return rr_below(&r, istart);
        throw_new(cls_ValueError, "empty range for randrange()");
    }
    istop = idx_of(stop);
    if (SMALLI(istart) && SMALLI(istop) && (!HAS(step) || (step.t == T_INT && step.u.i == 1))) {
        long w = istop.u.i - istart.u.i;
        if (w > 0) return mkint(istart.u.i + rr_below(&r, mkint(w)).u.i);
        throw_new(cls_ValueError, "empty range in randrange(%s, %s)", AS_STR(py_str(start))->s,
                  AS_STR(py_str(stop))->s);
    }
    width = int_sub(istop, istart);
    istep = HAS(step) ? idx_of(step) : mkint(1);
    if (istep.t == T_INT && istep.u.i == 1) {
        if (int_sign(width) > 0) return int_add(istart, rr_below(&r, width));
        throw_new(cls_ValueError, "empty range in randrange(%s, %s)", AS_STR(py_str(start))->s,
                  AS_STR(py_str(stop))->s);
    }
    if (int_sign(istep) > 0) n = int_floordiv(int_sub(int_add(width, istep), mkint(1)), istep);
    else if (int_sign(istep) < 0) n = int_floordiv(int_add(int_add(width, istep), mkint(1)), istep);
    else throw_new(cls_ValueError, "zero step for randrange()");
    if (int_sign(n) <= 0)
        throw_new(cls_ValueError, "empty range in randrange(%s, %s, %s)", AS_STR(py_str(start))->s,
                  AS_STR(py_str(stop))->s, AS_STR(py_str(step))->s);
    return int_add(istart, int_mul(istep, rr_below(&r, n)));
}
static Value pyf_randint(Value self, Args *x, int off)
{
    Value a = need_arg(x, off, 0, "a", "randint"), b = need_arg(x, off, 1, "b", "randint");
    Rn r;
    Value ia, ib;
    rn_init(&r, self);
    ib = int_add(idx_of(b), mkint(1));
    ia = idx_of(a);
    if (int_cmp(ib, ia) <= 0)
        throw_new(cls_ValueError, "empty range in randrange(%s, %s)", AS_STR(py_str(a))->s,
                  AS_STR(py_str(ib))->s);
    return int_add(ia, rr_below(&r, int_sub(ib, ia)));
}

/* ---- sequences -------------------------------------------------------------------------- */
static Value seq_get(Value seq, long i)
{
    if (seq.t == T_LIST || seq.t == T_TUPLE) {
        if (i < 0 || i >= AS_LIST(seq)->n) throw_new(cls_IndexError, "list index out of range");
        return AS_LIST(seq)->a[i];
    }
    return subscr(seq, mkint(i));
}
static Value pyf_choice(Value self, Args *x, int off)
{
    Rn r;
    Value seq = need_arg(x, off, 0, "seq", "choice"), idx;
    long n;
    rn_init(&r, self);
    n = py_len(seq);
    if (n == 0) throw_new(cls_IndexError, "Cannot choose from an empty sequence");
    idx = rr_below(&r, mkint(n));
    return seq.t == T_LIST || seq.t == T_TUPLE ? seq_get(seq, idx.u.i) : subscr(seq, idx);
}
static Value pyf_shuffle(Value self, Args *x, int off)
{
    Rn r;
    Value seq = need_arg(x, off, 0, "x", "shuffle");
    long n, i, j;
    rn_init(&r, self);
    n = py_len(seq);
    for (i = n - 1; i >= 1; i--) {
        j = rr_below(&r, mkint(i + 1)).u.i;
        if (seq.t == T_LIST) {
            List *l = AS_LIST(seq);
            Value t;
            if (i >= l->n || j >= l->n) throw_new(cls_IndexError, "list index out of range");
            t = l->a[i]; l->a[i] = l->a[j]; l->a[j] = t;
        } else {
            Value a = subscr(seq, mkint(i)), b = subscr(seq, mkint(j));
            VPUSH(a); VPUSH(b);
            setitem(seq, mkint(i), b);
            setitem(seq, mkint(j), a);
            vsp -= 2;
        }
    }
    return NONE_V;
}
static int is_sequence(Value v)
{
    switch (v.t) {
    case T_LIST: case T_TUPLE: case T_STR: case T_BYTES: case T_BARR: case T_RANGE: return 1;
    case T_INST:
        return class_find(AS_INST(v)->cls, intern_c("__getitem__")) != NULL &&
            class_find(AS_INST(v)->cls, intern_c("__len__")) != NULL &&
            !(AS_INST(v)->pay.t == T_DICT || AS_INST(v)->pay.t == T_SET);
    default: return 0;
    }
}
/* k distinct indices from range(n), in selection order (the pool / set algorithm of sample) */
static long *sample_idx(Rn *r, long n, long k)
{
    long *res = (long *)xmalloc(sizeof(long) * (size_t)(k + 1)), setsize = 21, i, j;
    if (k > 5) {
        double lg = ceil(log((double)k * 3.0) / log(4.0));
        long p = 1, e;
        for (e = 0; e < (long)lg; e++) p *= 4;
        setsize += p;
    }
    if (n <= setsize) {
        long *pool = (long *)xmalloc(sizeof(long) * (size_t)(n + 1));
        for (i = 0; i < n; i++) pool[i] = i;
        for (i = 0; i < k; i++) {
            j = rr_below(r, mkint(n - i)).u.i;
            res[i] = pool[j];
            pool[j] = pool[n - i - 1];
        }
        free(pool);
    } else {
        unsigned long cap = 16, mask;
        long *hs;
        while (cap < (unsigned long)k * 3UL) cap <<= 1;
        mask = cap - 1;
        hs = (long *)xmalloc(sizeof(long) * (size_t)cap);
        for (i = 0; i < (long)cap; i++) hs[i] = -1;
        for (i = 0; i < k; i++) {
            for (;;) {
                unsigned long h;
                j = rr_below(r, mkint(n)).u.i;
                h = (unsigned long)j * 2654435761UL & mask;
                while (hs[h] != -1 && hs[h] != j) h = (h + 1UL) & mask;
                if (hs[h] == j) continue;
                hs[h] = j;
                break;
            }
            res[i] = j;
        }
        free(hs);
    }
    return res;
}
static Value pyf_sample(Value self, Args *x, int off)
{
    Rn r;
    Value pop = need_arg(x, off, 0, "population", "sample"), kv = ARG(1, "k"), counts = kwget(x,
        "counts");
    long n, k, i, *idx;
    List *out;
    rn_init(&r, self);
    if (!HAS(kv)) throw_new(cls_TypeError,
        "Random.sample() missing 1 required positional argument: 'k'");
    if (!is_sequence(pop))
        throw_new(cls_TypeError,
            "Population must be a sequence.  For dicts or sets, use sorted(d).");
    n = py_len(pop);
    if (HAS(counts) && counts.t != T_NONE) {
        Value cl = iter_to_list(counts), total;
        long m = AS_LIST(cl)->n;
        Value *cum;
        long *sel, tot;
        if (m != n) throw_new(cls_ValueError, "The number of counts does not match the population");
        cum = (Value *)xmalloc(sizeof(Value) * (size_t)(m + 1));
        VPUSH(cl);
        total = mkint(0);
        for (i = 0; i < m; i++) {
            Value c = AS_LIST(cl)->a[i];
            if (i == 0) total = c; else total = is_intlike(total) &&
                is_intlike(c) ? int_add(idx_of(total), idx_of(c)) : mkfloat(dnum(total) + dnum(c));
            cum[i] = total;
        }
        vsp--;
        if (m == 0) total = mkint(0);
        if (!is_intlike(total)) { free(cum); throw_new(cls_TypeError, "Counts must be integers"); }
        if (int_sign(idx_of(total)) < 0) { free(cum); throw_new(cls_ValueError,
            "Counts must be non-negative"); }
        tot = need_long(total, "total");
        k = need_long(idx_of(kv), "k");
        if (k < 0 || k > tot) { free(cum); throw_new(cls_ValueError,
            "Sample larger than population or is negative"); }
        sel = sample_idx(&r, tot, k);
        out = list_new(k);
        VPUSH(mklist(out));
        for (i = 0; i < k; i++) {
            long lo = 0, hi = m > 0 ? m - 1 : 0;
            /* bisect_right(cum_counts, s) over cum_counts[:-1] */
            while (lo < hi) {
                long mid = (lo + hi) / 2;
                if (dnum(mkint(sel[i])) < dnum(cum[mid])) hi = mid; else lo = mid + 1;
            }
            list_push(out, seq_get(pop, lo));
        }
        vsp--;
        free(sel); free(cum);
        return mklist(out);
    }
    k = need_long(idx_of(kv), "k");
    if (k < 0 || k > n) throw_new(cls_ValueError, "Sample larger than population or is negative");
    idx = sample_idx(&r, n, k);
    out = list_new(k);
    VPUSH(mklist(out));
    for (i = 0; i < k; i++) list_push(out, seq_get(pop, idx[i]));
    vsp--;
    free(idx);
    return mklist(out);
}
static Value pyf_choices(Value self, Args *x, int off)
{
    Rn r;
    Value pop = need_arg(x, off, 0, "population", "choices"), weights = ARG(1, "weights"),
        cumw = kwget(x, "cum_weights"), kv = kwget(x, "k");
    long n, k = 1, i;
    List *out;
    double total;
    double *cw = NULL;
    rn_init(&r, self);
    if (x->n - off > 2)
        throw_new(cls_TypeError,
            "choices() takes from 2 to 3 positional arguments but %d were given", x->n - off + 1);
    if (HAS(kv)) k = need_long(idx_of(kv), "k");
    n = py_len(pop);
    out = list_new(k > 0 ? k : 0);
    VPUSH(mklist(out));
    if (NOARG(cumw)) {
        if (NOARG(weights)) {
            double nf = (double)n + 0.0;
            for (i = 0; i < k; i++) {
                double u = rg_rand(&r) * nf;
                list_push(out, seq_get(pop, (long)floor(u)));
            }
            vsp--;
            return mklist(out);
        }
        {
            Value wl;
            long m, j;
            if (is_intlike(weights) && weights.t != T_INST) {
                throw_new(cls_TypeError, "The number of choices must be a keyword argument: k=%s",
                          AS_STR(py_str(weights))->s);
            }
            wl = iter_to_list(weights);
            m = AS_LIST(wl)->n;
            cw = (double *)xmalloc(sizeof(double) * (size_t)(m + 1));
            {
                Value acc = mkint(0);
                for (j = 0; j < m; j++) {
                    Value c = AS_LIST(wl)->a[j];
                    if (j == 0) acc = c;
                    else if (is_intlike(acc) && is_intlike(c)) acc = int_add(idx_of(acc),
                        idx_of(c));
                    else acc = mkfloat(dnum(acc) + dnum(c));
                    cw[j] = dnum(acc);
                }
            }
            if (m != n) { free(cw); throw_new(cls_ValueError,
                "The number of weights does not match the population"); }
        }
    } else {
        Value wl;
        long m, j;
        if (!NOARG(weights)) throw_new(cls_TypeError,
            "Cannot specify both weights and cumulative weights");
        wl = iter_to_list(cumw);
        m = AS_LIST(wl)->n;
        cw = (double *)xmalloc(sizeof(double) * (size_t)(m + 1));
        for (j = 0; j < m; j++) cw[j] = dnum(AS_LIST(wl)->a[j]);
        if (m != n) { free(cw); throw_new(cls_ValueError,
            "The number of weights does not match the population"); }
    }
    total = (n > 0 ? cw[n - 1] : 0.0) + 0.0;
    if (n == 0) { free(cw); throw_new(cls_IndexError, "list index out of range"); }
    if (total <= 0.0) { free(cw); throw_new(cls_ValueError,
        "Total of weights must be greater than zero"); }
    if (total - total != 0.0) { free(cw); throw_new(cls_ValueError,
        "Total of weights must be finite"); }
    for (i = 0; i < k; i++) {
        double t = rg_rand(&r) * total;
        long lo = 0, hi = n - 1;
        while (lo < hi) {
            long mid = (lo + hi) / 2;
            if (t < cw[mid]) hi = mid; else lo = mid + 1;
        }
        list_push(out, seq_get(pop, lo));
    }
    free(cw);
    vsp--;
    return mklist(out);
}

/* ---- real-valued distributions ----------------------------------------------------------- */
#define TWOPI_C (2.0 * 3.14159265358979323846)
static double nv_magic(void) { return 4.0 * exp(-0.5) / sqrt(2.0); }
static Value pyf_uniform(Value self, Args *x, int off)
{
    Rn r;
    Value a = need_arg(x, off, 0, "a", "uniform"), b = need_arg(x, off, 1, "b", "uniform");
    double d;
    rn_init(&r, self);
    d = dsub(b, a);
    return mkfloat(dnum(a) + d * rg_rand(&r));
}
static Value pyf_triangular(Value self, Args *x, int off)
{
    Rn r;
    Value lo = ARG(0, "low"), hi = ARG(1, "high"), mode = ARG(2, "mode");
    double u, c, l, h;
    Value lov;
    rn_init(&r, self);
    lov = HAS(lo) ? lo : mkfloat(0.0);
    if (!HAS(hi)) hi = mkfloat(1.0);
    u = rg_rand(&r);
    if (NOARG(mode)) c = 0.5;
    else {
        double den = dsub(hi, lov);
        if (den == 0.0) return lov;
        c = dsub(mode, lov) / den;
    }
    l = dnum(lov); h = dnum(hi);
    if (u > c) {
        double t;
        u = 1.0 - u;
        c = 1.0 - c;
        t = l; l = h; h = t;
    }
    return mkfloat(l + (h - l) * sqrt(u * c));
}
static double normalvariate_d(Rn *r, double mu, double sigma)
{
    double u1, u2, z, zz, magic = nv_magic();
    for (;;) {
        u1 = rg_rand(r);
        u2 = 1.0 - rg_rand(r);
        z = magic * (u1 - 0.5) / u2;
        zz = z * z / 4.0;
        if (zz <= -flog(u2)) break;
    }
    return mu + z * sigma;
}
static Value pyf_normalvariate(Value self, Args *x, int off)
{
    Rn r;
    Value m = ARG(0, "mu"), s = ARG(1, "sigma");
    rn_init(&r, self);
    return mkfloat(normalvariate_d(&r, HAS(m) ? dnum(m) : 0.0, HAS(s) ? dnum(s) : 1.0));
}
static Value pyf_gauss(Value self, Args *x, int off)
{
    Rn r;
    Value m = ARG(0, "mu"), s = ARG(1, "sigma"), zv;
    double mu, sigma, z;
    rn_init(&r, self);
    mu = HAS(m) ? dnum(m) : 0.0;
    sigma = HAS(s) ? dnum(s) : 1.0;
    zv = get_gauss_next(self);
    set_gauss_next(self, NONE_V);
    if (zv.t == T_NONE || zv.t == T_UNSET) {
        double x2pi = rg_rand(&r) * TWOPI_C, g2rad;
        g2rad = sqrt(-2.0 * flog(1.0 - rg_rand(&r)));
        z = cos(x2pi) * g2rad;
        set_gauss_next(self, mkfloat(sin(x2pi) * g2rad));
    } else z = dnum(zv);
    return mkfloat(mu + z * sigma);
}
static Value pyf_lognormvariate(Value self, Args *x, int off)
{
    Rn r;
    Value m = need_arg(x, off, 0, "mu", "lognormvariate"), s = need_arg(x, off, 1, "sigma",
        "lognormvariate");
    double v;
    rn_init(&r, self);
    v = normalvariate_d(&r, dnum(m), dnum(s));
    return mkfloat(exp(v));
}
static Value pyf_expovariate(Value self, Args *x, int off)
{
    Rn r;
    Value l = ARG(0, "lambd");
    double lambd = HAS(l) ? dnum(l) : 1.0;
    rn_init(&r, self);
    return mkfloat(fdiv0(-flog(1.0 - rg_rand(&r)), lambd));
}
static Value pyf_vonmises(Value self, Args *x, int off)
{
    Rn r;
    double mu, kappa, s, rr, u1, u2, u3, z, d, q, f, theta;
    ARGD(mu, 0, "mu", "vonmisesvariate");
    ARGD(kappa, 1, "kappa", "vonmisesvariate");
    rn_init(&r, self);
    if (kappa <= 1e-6) return mkfloat(TWOPI_C * rg_rand(&r));
    s = 0.5 / kappa;
    rr = s + sqrt(1.0 + s * s);
    for (;;) {
        u1 = rg_rand(&r);
        z = cos(3.14159265358979323846 * u1);
        d = z / (rr + z);
        u2 = rg_rand(&r);
        if (u2 < 1.0 - d * d || u2 <= (1.0 - d) * exp(d)) break;
    }
    q = 1.0 / rr;
    f = (q + z) / (1.0 + q * z);
    u3 = rg_rand(&r);
    if (f > 1.0 || f < -1.0) throw_new(cls_ValueError, "math domain error");
    if (u3 > 0.5) theta = fmodpy(mu + acos(f), TWOPI_C);
    else theta = fmodpy(mu - acos(f), TWOPI_C);
    return mkfloat(theta);
}
static double gammavariate_d(Rn *r, double alpha, double beta)
{
    double ainv, bbb, ccc, u1, u2, v, xx, z, rr, u, b, p, e = 2.71828182845904523536;
    if (alpha <= 0.0 || beta <= 0.0)
        throw_new(cls_ValueError, "gammavariate: alpha and beta must be > 0.0");
    if (alpha > 1.0) {
        ainv = sqrt(2.0 * alpha - 1.0);
        bbb = alpha - log(4.0);
        ccc = alpha + ainv;
        for (;;) {
            u1 = rg_rand(r);
            if (!(1e-7 < u1 && u1 < 0.9999999)) continue;
            u2 = 1.0 - rg_rand(r);
            v = flog(fdiv0(u1, 1.0 - u1)) / ainv;
            xx = alpha * exp(v);
            z = u1 * u1 * u2;
            rr = bbb + ccc * v - xx;
            if (rr + (1.0 + log(4.5)) - 4.5 * z >= 0.0 || rr >= flog(z)) return xx * beta;
        }
    } else if (alpha == 1.0) {
        return -flog(1.0 - rg_rand(r)) * beta;
    }
    for (;;) {
        u = rg_rand(r);
        b = (e + alpha) / e;
        p = b * u;
        if (p <= 1.0) xx = fpow(p, 1.0 / alpha);
        else xx = -flog((b - p) / alpha);
        u1 = rg_rand(r);
        if (p > 1.0) {
            if (u1 <= fpow(xx, alpha - 1.0)) break;
        } else if (u1 <= exp(-xx)) break;
    }
    return xx * beta;
}
static Value pyf_gammavariate(Value self, Args *x, int off)
{
    Rn r;
    double a, b;
    ARGD(a, 0, "alpha", "gammavariate");
    ARGD(b, 1, "beta", "gammavariate");
    rn_init(&r, self);
    return mkfloat(gammavariate_d(&r, a, b));
}
static Value pyf_betavariate(Value self, Args *x, int off)
{
    Rn r;
    double a, b, y;
    ARGD(a, 0, "alpha", "betavariate");
    ARGD(b, 1, "beta", "betavariate");
    rn_init(&r, self);
    y = gammavariate_d(&r, a, 1.0);
    if (y != 0.0) return mkfloat(y / (y + gammavariate_d(&r, b, 1.0)));
    return mkfloat(0.0);
}
static Value pyf_paretovariate(Value self, Args *x, int off)
{
    Rn r;
    double a, u;
    ARGD(a, 0, "alpha", "paretovariate");
    rn_init(&r, self);
    u = 1.0 - rg_rand(&r);
    return mkfloat(fpow(u, fdiv0(-1.0, a)));
}
static Value pyf_weibullvariate(Value self, Args *x, int off)
{
    Rn r;
    double a, b, u;
    ARGD(a, 0, "alpha", "weibullvariate");
    ARGD(b, 1, "beta", "weibullvariate");
    rn_init(&r, self);
    u = 1.0 - rg_rand(&r);
    return mkfloat(a * fpow(-flog(u), fdiv0(1.0, b)));
}
static Value pyf_binomialvariate(Value self, Args *x, int off)
{
    Rn r;
    Value nv = ARG(0, "n"), pv = ARG(1, "p");
    long n, k, m = 0, sub = 0;
    double p, spq, b, a, c, vr, u, us, v, alpha = 0.0, lpq = 0.0, h = 0.0;
    int setup = 0;
    rn_init(&r, self);
    n = HAS(nv) ? need_long(idx_of(nv), "n") : 1;
    p = HAS(pv) ? dnum(pv) : 0.5;
    if (n < 0) throw_new(cls_ValueError, "n must be non-negative");
    if (p <= 0.0 || p >= 1.0) {
        if (p == 0.0) return mkint(0);
        if (p == 1.0) return mkint(n);
        throw_new(cls_ValueError, "p must be in the range 0.0 <= p <= 1.0");
    }
    if (n == 1) return mkint(rg_rand(&r) < p ? 1 : 0);
    if (p > 0.5) { sub = 1; p = 1.0 - p; }
    if ((double)n * p < 10.0) {
        long xx = 0, y = 0;
        double c2 = log2_d(1.0 - p);
        if (c2 == 0.0) return mkint(sub ? n - xx : xx);
        for (;;) {
            double rv = rg_rand(&r);
            if (rv <= 0.0) continue;
            y += (long)floor(log2_d(rv) / c2) + 1;
            if (y > n) return mkint(sub ? n - xx : xx);
            xx++;
        }
    }
    spq = sqrt((double)n * p * (1.0 - p));
    b = 1.15 + 2.53 * spq;
    a = -0.0873 + 0.0248 * b + 0.01 * p;
    c = (double)n * p + 0.5;
    vr = 0.92 - 4.2 / b;
    for (;;) {
        u = rg_rand(&r);
        u -= 0.5;
        us = 0.5 - fabs(u);
        if (us == 0.0) continue;
        k = (long)floor((2.0 * a / us + b) * u + c);
        if (k < 0 || k > n) continue;
        v = rg_rand(&r);
        if (us >= 0.07 && v <= vr) return mkint(sub ? n - k : k);
        if (!setup) {
            alpha = (2.83 + 5.1 / b) * spq;
            lpq = flog(p / (1.0 - p));
            m = (long)floor((double)(n + 1) * p);
            h = d_lgamma((double)m + 1.0) + d_lgamma((double)(n - m) + 1.0);
            setup = 1;
        }
        v *= alpha / (a / (us * us) + b);
        if (flog(v) <= h - d_lgamma((double)k + 1.0) - d_lgamma((double)(n - k) + 1.0) +
            (double)(k - m) * lpq)
            return mkint(sub ? n - k : k);
    }
}

/* ---- __init__, SystemRandom, registration ------------------------------------------------- */
BI(pr_init)
{
    Value seedf, xv;
    if (x->n < 1 || A(0).t != T_INST) throw_new(cls_TypeError,
        "descriptor '__init__' requires a 'random.Random' object");
    xv = x->n > 1 ? A(1) : kwget(x, "x");
    if (x->n > 2) throw_new(cls_TypeError, "Random() takes at most 1 argument (%d given)",
        x->n - 1);
    seedf = get_attr(A(0), S_pr_seed);
    VPUSH(seedf);
    call1(seedf, HAS(xv) ? xv : NONE_V);
    vsp--;
    set_gauss_next(A(0), NONE_V);
    return NONE_V;
}
static double sys_random(void)
{
    unsigned char b[7];
    unsigned long hi, lo;
    host_urandom(b, 7);
    hi = ((unsigned long)b[0] << 16) | ((unsigned long)b[1] << 8) | (unsigned long)b[2];
    lo = ((unsigned long)b[3] << 24) | ((unsigned long)b[4] << 16) |
        ((unsigned long)b[5] << 8) | (unsigned long)b[6];
    /* int.from_bytes(urandom(7), 'big') >> 3: a 53-bit integer */
    return floor(((double)hi * 4294967296.0 + (double)lo) / 8.0) * (1.0 / 9007199254740992.0);
}
BI(sr_random) { (void)x; return mkfloat(sys_random()); }
BI(sr_getrandbits)
{
    long k, nb, i;
    unsigned char *b;
    unsigned long *d;
    Value v;
    if (x->n < 2) throw_new(cls_TypeError, "getrandbits() missing required argument 'k' (pos 1)");
    k = need_long(idx_of(A(1)), "k");
    if (k < 0) throw_new(cls_ValueError, "number of bits must be non-negative");
    if (k == 0) return mkint(0);
    nb = (k + 7) / 8;
    b = (unsigned char *)xmalloc((size_t)nb);
    host_urandom(b, nb);
    /* x = int.from_bytes(b, 'big') >> (nb * 8 - k): drop the low (nb*8-k) bits */
    d = (unsigned long *)xmalloc(sizeof(unsigned long) * (size_t)(nb + 2));
    {
        int drop = (int)(nb * 8 - k);
        long nd = 0;
        unsigned char *t = (unsigned char *)xmalloc((size_t)nb + 1);
        long j;
        /* little-endian copy, shifted right by `drop` bits */
        for (j = 0; j < nb; j++) t[j] = b[nb - 1 - j];
        t[nb] = 0;
        for (j = 0; j < nb; j++)
            t[j] = (unsigned char)(((t[j] >> drop) | ((unsigned)t[j + 1] << (8 - drop))) & 0xFF);
        for (i = 0; 2 * i < nb; i++) {
            d[i] = t[2 * i] | (2 * i + 1 < nb ? (unsigned long)t[2 * i + 1] << 8 : 0UL);
            nd++;
        }
        v = int_from_bin(d, (int)nd, 0);
        free(t);
    }
    free(d);
    free(b);
    return v;
}
BI(sr_randbytes)
{
    long n;
    unsigned char *b;
    Value r;
    if (x->n < 2) throw_new(cls_TypeError, "randbytes() missing required argument 'n' (pos 1)");
    n = need_long(idx_of(A(1)), "n");
    if (n < 0) throw_new(cls_ValueError, "negative argument not allowed");
    b = (unsigned char *)xmalloc((size_t)n + 1);
    host_urandom(b, n);
    r = mkbytes((const char *)b, n);
    free(b);
    return r;
}
BI(sr_seed) { (void)x; return NONE_V; }
BI(sr_state) { (void)x; throw_new(cls_NotImplementedError,
    "System entropy source does not have state."); return NONE_V; }

#define PYR_METHOD(nm) \
    BI(pm_##nm) \
    { \
        if (x->n < 1) throw_new(cls_TypeError, "descriptor '" #nm "' needs an object"); \
        return pyf_##nm(A(0), x, 1); \
    } \
    BI(pf_##nm) { return pyf_##nm(pr_inst, x, 0); }
PYR_METHOD(seed) PYR_METHOD(getstate) PYR_METHOD(setstate) PYR_METHOD(random)
PYR_METHOD(getrandbits) PYR_METHOD(randbytes) PYR_METHOD(randbelow) PYR_METHOD(randrange)
PYR_METHOD(randint) PYR_METHOD(choice) PYR_METHOD(shuffle) PYR_METHOD(sample)
PYR_METHOD(choices) PYR_METHOD(uniform) PYR_METHOD(triangular)
PYR_METHOD(normalvariate) PYR_METHOD(gauss) PYR_METHOD(lognormvariate) PYR_METHOD(expovariate)
PYR_METHOD(vonmises) PYR_METHOD(gammavariate) PYR_METHOD(betavariate) PYR_METHOD(paretovariate)
PYR_METHOD(weibullvariate) PYR_METHOD(binomialvariate)

BI(pf_sha512)
{
    unsigned char dg[64];
    Value a;
    if (x->n < 1) throw_new(cls_TypeError, "sha512() missing required argument");
    a = A(0);
    if (a.t == T_BYTES || a.t == T_STR) sha512((const unsigned char *)AS_STR(a)->s, AS_STR(a)->len,
        dg);
    else if (a.t == T_BARR) sha512((const unsigned char *)AS_BARR(a)->s, AS_BARR(a)->n, dg);
    else throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(a));
    return mkbytes((const char *)dg, 64);
}

static void pyrand_init(void)
{
    Module *m = new_module("random");
    Class **b = (Class **)xmalloc(sizeof(Class *));
    Class **b2 = (Class **)xmalloc(sizeof(Class *));
    Value sysinst;
    S_pr_s = intern_c("_s"); S_pr_gauss = intern_c("gauss_next"); S_pr_random = intern_c("random");
    S_pr_getrandbits = intern_c("getrandbits"); S_pr_randbelow = intern_c("_randbelow");
    S_pr_seed = intern_c("seed");
    b[0] = cls_object;
    cls_pyrand = make_class(intern_c("Random"), b, 1, intern_c("random"));
    b2[0] = cls_pyrand;
    cls_sysrand = make_class(intern_c("SystemRandom"), b2, 1, intern_c("random"));
#define PM(py, nm) def_bi(cls_pyrand, py, pm_##nm)
    PM("seed", seed); PM("getstate", getstate); PM("setstate", setstate); PM("random", random);
    PM("getrandbits", getrandbits); PM("randbytes", randbytes); PM("_randbelow", randbelow);
    PM("randrange", randrange); PM("randint", randint); PM("choice", choice); PM("shuffle",
        shuffle);
    PM("sample", sample); PM("choices", choices); PM("uniform", uniform); PM("triangular",
        triangular);
    PM("normalvariate", normalvariate); PM("gauss", gauss); PM("lognormvariate", lognormvariate);
    PM("expovariate", expovariate); PM("vonmisesvariate", vonmises); PM("gammavariate",
        gammavariate);
    PM("betavariate", betavariate); PM("paretovariate", paretovariate);
    PM("weibullvariate", weibullvariate); PM("binomialvariate", binomialvariate);
#undef PM
    def_bi(cls_pyrand, "__init__", pr_init);
    amap_set(&cls_pyrand->ns, intern_c("VERSION"), mkint(3));
    bi_pr_random = *amap_get(&cls_pyrand->ns, S_pr_random);
    bi_pr_getrandbits = *amap_get(&cls_pyrand->ns, S_pr_getrandbits);
    bi_pr_randbelow = *amap_get(&cls_pyrand->ns, S_pr_randbelow);
    def_bi(cls_sysrand, "random", sr_random); def_bi(cls_sysrand, "getrandbits", sr_getrandbits);
    def_bi(cls_sysrand, "randbytes", sr_randbytes); def_bi(cls_sysrand, "seed", sr_seed);
    def_bi(cls_sysrand, "getstate", sr_state); def_bi(cls_sysrand, "setstate", sr_state);
    mod_val(m, "Random", mkobj(cls_pyrand, T_CLASS));
    mod_val(m, "SystemRandom", mkobj(cls_sysrand, T_CLASS));
    {
        Args none;
        none.n = 0; none.a = NULL; none.nk = 0; none.kw = NULL;
        pr_inst = call_value(mkobj(cls_pyrand, T_CLASS), 0, NULL, 0, NULL);
        (void)none;
    }
    mod_val(m, "_inst", pr_inst);
    sysinst = UNSET_V;
    (void)sysinst;
#define PF(py, nm) mod_def(m, py, pf_##nm)
    PF("seed", seed); PF("getstate", getstate); PF("setstate", setstate); PF("random", random);
    PF("getrandbits", getrandbits); PF("randbytes", randbytes); PF("randrange", randrange);
    PF("randint", randint); PF("choice", choice); PF("shuffle", shuffle); PF("sample", sample);
    PF("choices", choices); PF("uniform", uniform); PF("triangular", triangular);
    PF("normalvariate", normalvariate); PF("gauss", gauss); PF("lognormvariate", lognormvariate);
    PF("expovariate", expovariate); PF("vonmisesvariate", vonmises); PF("gammavariate",
        gammavariate);
    PF("betavariate", betavariate); PF("paretovariate", paretovariate);
    PF("weibullvariate", weibullvariate); PF("binomialvariate", binomialvariate);
#undef PF
    mod_def(m, "_sha512", pf_sha512);
    mod_def(m, "_randbelow", pf_randbelow);
    mod_val(m, "TWOPI", mkfloat(TWOPI_C));
    mod_val(m, "NV_MAGICCONST", mkfloat(nv_magic()));
    mod_val(m, "LOG4", mkfloat(log(4.0)));
    mod_val(m, "SG_MAGICCONST", mkfloat(1.0 + log(4.5)));
    mod_val(m, "BPF", mkint(53));
    mod_val(m, "RECIP_BPF", mkfloat(1.0 / 9007199254740992.0));
}
