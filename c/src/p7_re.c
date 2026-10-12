/* =========================================================================
   p7_re.c   native re module: parser, compiler and a backtracking VM with
             an explicit stack (no C recursion per character), plus the
             Pattern / Match objects and the module functions.

   Strings are bytes (UTF-8): "." and character classes consume one whole
   UTF-8 character, offsets are byte offsets.  Case folding is ASCII only;
   \d is ASCII only; \w and \s know a small approximation of Unicode.
   ========================================================================= */

#define RF_I 2
#define RF_L 4
#define RF_M 8
#define RF_S 16
#define RF_U 32
#define RF_X 64
#define RF_D 128
#define RF_A 256
#define RE_TYPEF (RF_A | RF_L | RF_U)
#define RE_STK_MAX 6000000L
#define RE_MAXREP 2147483647L
#define RE_MAXDEPTH 400

enum { RN_EMPTY, RN_LIT, RN_ANY, RN_SET, RN_CAT, RN_ALT, RN_GROUP, RN_REP, RN_AT, RN_BREF,
       RN_LOOK, RN_ATOMIC, RN_COND, RN_FAIL };
/* assertion kinds */
enum { RA_BOS, RA_BOL, RA_EOL, RA_EOLM, RA_EOS, RA_WB, RA_NWB, RA_WBA, RA_NWBA };
enum { RO_MATCH, RO_LIT, RO_LITI, RO_ANY, RO_SET, RO_SPLIT, RO_JMP, RO_SAVE, RO_SAVEEND, RO_AT,
       RO_BREF, RO_LOOK, RO_LOOKEND, RO_REP1, RO_CNTSET, RO_LOOP, RO_ENTER, RO_ENDITER,
       RO_CHKEMPTY, RO_CONDREF, RO_FAIL };
enum { RK_ALT, RK_CAP, RK_CAP2, RK_REPG, RK_REPL, RK_LOOK };
#define HF_ALL 1
#define HF_WORD 2
#define HF_NONWORD 4
#define HF_SPACE 8
#define HF_NONSPACE 16

typedef struct RI { long op, a, b, c, d, e; } RI;
typedef struct RSet {
    unsigned char bm[16];
    long hif, neg, roff, nrng, pure;
} RSet;
typedef struct RProg {
    long nins, nsets, nrng, litlen;
    long ngroups, ncaps, li, flags;
    long anchored, nullable, first_single, has_first;
    long off_ins, off_sets, off_rng, off_lit;
    unsigned char first[256];
} RProg;
#define RP_INS(pg) ((const RI *)((const char *)(pg) + (pg)->off_ins))
#define RP_SETS(pg) ((const RSet *)((const char *)(pg) + (pg)->off_sets))
#define RP_RNG(pg) ((const long *)((const char *)(pg) + (pg)->off_rng))
#define RP_LIT(pg) ((const unsigned char *)((const char *)(pg) + (pg)->off_lit))
#define BM_HAS(b, c) (((b)[(c) >> 3] >> ((c) & 7)) & 1)
#define BM_SET(b, c) ((b)[(c) >> 3] |= (unsigned char)(1 << ((c) & 7)))

typedef struct RNode {
    int type;
    long a, b, c;
    int kid, next;
    long wlo, whi;
    unsigned char lb[4];
} RNode;

typedef struct RTok {
    const unsigned char *s;
    long n, index, nstart;
    int nlen;
} RTok;

typedef struct RParse {
    RTok t;
    Value patv;
    long flags, gflags;
    int ng;
    unsigned char *gclosed;
    long *gwlo, *gwhi;
    int gcap;
    long *nm_off, *nm_len;
    int *nm_gid;
    int nnm, nmcap;
    long lbg;
    int *cr_gid;
    long *cr_pos;
    int ncr, crcap;
    RNode *nodes;
    int nn, ncap;
    RSet *sets;
    int nsets, setcap;
    long *rng;
    long nrng, rngcap;
    RI *ins;
    long nins, inscap;
    unsigned char *lit;
    long nlit, litcap;
    long nslots;
    int depth;
} RParse;

typedef struct RSE { long k, a, b, c; } RSE;

static Class *cls_re_pattern, *cls_re_match, *cls_re_error, *cls_re_iter, *cls_re_scanner;
static Str *RS_caps, *RS_prog, *RS_re, *RS_string, *RS_pos, *RS_endpos, *RS_lastindex,
    *RS_pattern, *RS_flags, *RS_groups, *RS_groupindex, *RS_msg, *RS_lineno, *RS_colno,
    *RS_it_p, *RS_it_s, *RS_it_pos, *RS_it_end, *RS_it_na;
static Value re_cache_v;
static void (*re_cleanup)(void *);
static void *re_cleanup_arg;
static RSE *rstk;
static long rstk_cap;
static long *rcaps;
static long rcaps_cap;

/* ---- UTF-8 and character predicates ---------------------------------- */
static int re_u8len(const unsigned char *s, long pos, long n)
{
    unsigned c = s[pos];
    long l;
    if (c < 0xC0) return 1;
    l = c < 0xE0 ? 2 : c < 0xF0 ? 3 : c < 0xF8 ? 4 : 1;
    if (l == 1 || pos + l > n) return 1;
    if ((s[pos + 1] & 0xC0) != 0x80) return 1;
    if (l > 2 && (s[pos + 2] & 0xC0) != 0x80) return 1;
    if (l > 3 && (s[pos + 3] & 0xC0) != 0x80) return 1;
    return (int)l;
}
static long re_u8dec(const unsigned char *s, long pos, long n, int *len)
{
    int l = re_u8len(s, pos, n);
    unsigned c = s[pos];
    *len = l;
    if (l == 1) return c < 128 ? (long)c : 0xDC00L + (long)c;
    if (l == 2) return ((long)(c & 0x1F) << 6) | (s[pos + 1] & 0x3F);
    if (l == 3)
        return ((long)(c & 0x0F) << 12) | ((long)(s[pos + 1] & 0x3F) << 6) | (s[pos + 2] & 0x3F);
    return ((long)(c & 0x07) << 18) | ((long)(s[pos + 1] & 0x3F) << 12) |
           ((long)(s[pos + 2] & 0x3F) << 6) | (s[pos + 3] & 0x3F);
}
static int re_u8enc(long cp, unsigned char *o)
{
    if (cp >= 0xDC80L && cp <= 0xDCFFL) { o[0] = (unsigned char)(cp - 0xDC00L); return 1; }
    if (cp < 0x80) { o[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) {
        o[0] = (unsigned char)(0xC0 | (cp >> 6)); o[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000L) {
        o[0] = (unsigned char)(0xE0 | (cp >> 12));
        o[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (unsigned char)(0xF0 | (cp >> 18));
    o[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}
static long re_back1(const unsigned char *s, long pos)
{
    int k = 0;
    pos--;
    while (pos > 0 && k < 3 && (s[pos] & 0xC0) == 0x80) { pos--; k++; }
    return pos;
}
static int re_cp_word(long cp)
{
    if (cp < 0x80) return ch_alnum[(int)cp];
    if (cp < 0xC0) {
        return cp == 0xAA || cp == 0xB5 || cp == 0xBA || cp == 0xB2 || cp == 0xB3 ||
               cp == 0xB9 || cp == 0xBC || cp == 0xBD || cp == 0xBE;
    }
    if (cp < 0x100) return cp != 0xD7 && cp != 0xF7;
    if (cp < 0x300) return 1;
    if (cp < 0x370) return 0;
    if (cp < 0x2000) return 1;
    if (cp < 0x2C00) return cp >= 0x2070 && cp <= 0x209F;
    if (cp < 0x2E00) return 1;
    if (cp < 0x3040) return cp >= 0x3005 && cp <= 0x3007;
    if (cp < 0x3200) return 1;
    if (cp < 0x3400) return 0;
    if (cp < 0xD800L) return 1;
    if (cp < 0xF900L) return 0;
    if (cp < 0xFE00L) return cp != 0xFD3EL && cp != 0xFD3FL;
    if (cp < 0xFE70L) return 0;
    if (cp < 0xFF00L) return 1;
    if (cp < 0xFFF0L) {
        if (cp <= 0xFF0FL) return 0;
        if (cp <= 0xFF19L) return 1;
        if (cp <= 0xFF20L) return 0;
        if (cp <= 0xFF3AL) return 1;
        if (cp <= 0xFF40L) return 0;
        if (cp <= 0xFF5AL) return 1;
        if (cp <= 0xFF65L) return 0;
        return cp <= 0xFFDCL;
    }
    if (cp < 0x10000L) return 0;
    if (cp >= 0x1D000L && cp < 0x1D400L) return 0;
    if (cp >= 0x1D800L && cp < 0x20000L) return 0;
    if (cp >= 0x40000L) return 0;
    return 1;
}
static int re_cp_space(long cp)
{
    return cp == 0x85 || cp == 0xA0 || cp == 0x1680L || (cp >= 0x2000L && cp <= 0x200AL) ||
           cp == 0x2028L || cp == 0x2029L || cp == 0x202FL || cp == 0x205FL || cp == 0x3000L;
}
static int re_isword_after(const unsigned char *s, long pos, long n, int ascii)
{
    int l;
    unsigned c;
    if (pos >= n) return 0;
    c = s[pos];
    if (c < 128) return ch_alnum[c];
    if (ascii) return 0;
    return re_cp_word(re_u8dec(s, pos, n, &l));
}
static int re_isword_before(const unsigned char *s, long pos, int ascii)
{
    int l;
    unsigned c;
    long q;
    if (pos <= 0) return 0;
    c = s[pos - 1];
    if (c < 128) return ch_alnum[c];
    if (ascii) return 0;
    q = re_back1(s, pos);
    return re_cp_word(re_u8dec(s, q, pos, &l));
}
static int re_rset_hi(const RSet *st, const long *rng, long cp)
{
    int hit = 0;
    long f = st->hif, i;
    if (f & HF_ALL) hit = 1;
    else if ((f & HF_WORD) && re_cp_word(cp)) hit = 1;
    else if ((f & HF_NONWORD) && !re_cp_word(cp)) hit = 1;
    else if ((f & HF_SPACE) && re_cp_space(cp)) hit = 1;
    else if ((f & HF_NONSPACE) && !re_cp_space(cp)) hit = 1;
    else {
        for (i = 0; i < st->nrng; i++) {
            if (cp >= rng[2 * (st->roff + i)] && cp <= rng[2 * (st->roff + i) + 1]) { hit = 1;
                break;
            }
        }
    }
    return hit != (st->neg != 0);
}
/* length in bytes of the character at pos matched by set st, or 0 */
static int re_setmatch(const RSet *st, const long *rng, const unsigned char *s, long pos, long n)
{
    unsigned c = s[pos];
    int l;
    long cp;
    if (c < 128) return BM_HAS(st->bm, c) ? 1 : 0;
    if (st->pure) return 0;
    cp = re_u8dec(s, pos, n, &l);
    return re_rset_hi(st, rng, cp) ? l : 0;
}

/* ---- error helpers ---------------------------------------------------- */
static void re_error_fill(Inst *in, Value msgv, Value patv, long pos, int haspos)
{
    Str *ms = AS_STR(msgv);
    Buf b;
    Value full;
    long lineno = 1, colno = 0, i;
    int hasnl = 0;
    List *t;
    buf_init(&b);
    buf_addn(&b, ms->s, ms->len);
    if (haspos && patv.t == T_STR) {
        Str *ps = AS_STR(patv);
        char tmp[64];
        long last = -1;
        sprintf(tmp, " at position %ld", pos);
        buf_add(&b, tmp);
        for (i = 0; i < ps->len; i++) {
            if (ps->s[i] == '\n') {
                hasnl = 1;
                if (i < pos) { lineno++; last = i; }
            }
        }
        colno = pos - last;
        if (hasnl) {
            sprintf(tmp, " (line %ld, column %ld)", lineno, colno);
            buf_add(&b, tmp);
        }
    }
    full = buf_value(&b);
    VPUSH(full);
    t = tuple_new(1);
    t->a[0] = full;
    amap_set(&in->a, S_args, mkobj(t, T_TUPLE));
    amap_set(&in->a, RS_msg, msgv);
    amap_set(&in->a, RS_pattern, haspos ? patv : NONE_V);
    amap_set(&in->a, RS_pos, haspos ? mkint(pos) : NONE_V);
    amap_set(&in->a, RS_lineno, haspos ? mkint(lineno) : NONE_V);
    amap_set(&in->a, RS_colno, haspos ? mkint(colno) : NONE_V);
    vsp--;
}
static void re_throw_error(const char *msg, Value patv, long pos, int haspos)
{
    Inst *in = new_inst(cls_re_error);
    Value iv = mkobj(in, T_INST), mv;
    VPUSH(iv);
    mv = mkcstr(msg);
    VPUSH(mv);
    re_error_fill(in, mv, patv, pos, haspos);
    vsp -= 2;
    throw_val(iv);
}
static void re_cleanup_run(void)
{
    if (re_cleanup) {
        void (*f)(void *) = re_cleanup;
        re_cleanup = NULL;
        f(re_cleanup_arg);
    }
}
static void re_throw_cls(Class *c, const char *msg)
{
    re_cleanup_run();
    throw_new(c, "%s", msg);
}
static void re_pfail_at(Value patv, const char *msg, long pos)
{
    re_cleanup_run();
    re_throw_error(msg, patv, pos, 1);
}

/* ---- tokenizer (mirrors re._parser.Tokenizer; tokens are characters or \x pairs) --------- */
static void rt_advance(RTok *t, Value patv)
{
    long i = t->index;
    if (i >= t->n) { t->nstart = i; t->nlen = 0; return; }
    if (t->s[i] == '\\') {
        if (i + 1 >= t->n) re_pfail_at(patv, "bad escape (end of pattern)", t->n - 1);
        t->nlen = 1 + re_u8len(t->s, i + 1, t->n);
    } else {
        t->nlen = re_u8len(t->s, i, t->n);
    }
    t->nstart = i;
    t->index = i + t->nlen;
}
static void rt_init(RTok *t, const unsigned char *s, long n, Value patv)
{
    t->s = s; t->n = n; t->index = 0; t->nstart = 0; t->nlen = 0;
    rt_advance(t, patv);
}
#define RT_NEXT1(t, c) ((t)->nlen == 1 && (t)->s[(t)->nstart] == (unsigned char)(c))
static int rt_get(RTok *t, Value patv, long *st, int *len)
{
    if (t->nlen == 0) return 0;
    *st = t->nstart; *len = t->nlen;
    rt_advance(t, patv);
    return 1;
}
static int rt_match(RTok *t, Value patv, int c)
{
    if (RT_NEXT1(t, c)) { rt_advance(t, patv); return 1; }
    return 0;
}
static void rt_seek(RTok *t, Value patv, long pos)
{
    t->index = pos;
    rt_advance(t, patv);
}
static int re_isident(const unsigned char *s, long n)
{
    long i;
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) {
        unsigned c = s[i];
        if (c >= 128) continue;
        if (i == 0 ? !ch_alpha[c] : !ch_alnum[c]) return 0;
    }
    return 1;
}
static int re_isdigitc(int c)
{
    return c >= 0 && c < 256 && ch_digit[c];
}
static int re_isoctc(int c)
{
    return c >= 0 && c < 256 && ch_digit[c] && c != '8' && c != '9';
}

/* ---- parser state ----------------------------------------------------- */
static void rp_free(RParse *P)
{
    free(P->gclosed); free(P->gwlo); free(P->gwhi); free(P->nm_off); free(P->nm_len);
    free(P->nm_gid); free(P->cr_gid); free(P->cr_pos); free(P->nodes); free(P->sets);
    free(P->rng); free(P->ins); free(P->lit);
    P->gclosed = NULL; P->gwlo = P->gwhi = NULL; P->nm_off = P->nm_len = NULL; P->nm_gid = NULL;
    P->cr_gid = NULL; P->cr_pos = NULL; P->nodes = NULL; P->sets = NULL; P->rng = NULL;
    P->ins = NULL; P->lit = NULL;
}
static void rp_cleanup_cb(void *v)
{
    rp_free((RParse *)v);
}
/* error at (tell - offset) */
static void rp_fail(RParse *P, const char *msg, long offset)
{
    re_pfail_at(P->patv, msg, P->t.nstart - offset);
}
static void rp_failf(RParse *P, long offset, const char *fmt, const char *arg, long num)
{
    char buf[700];
    char a[300];
    long n = arg ? (long)strlen(arg) : 0;
    if (n > 250) n = 250;
    if (arg) { memcpy(a, arg, (size_t)n); }
    a[n] = 0;
    if (arg && num >= 0) sprintf(buf, fmt, a, num);
    else if (arg) sprintf(buf, fmt, a);
    else sprintf(buf, fmt, num);
    rp_fail(P, buf, offset);
}
static Value re_repr_range(const unsigned char *s, long off, long len)
{
    return py_repr(mkstr((const char *)s + off, len));
}
static int rn_new(RParse *P, int type)
{
    RNode *nd;
    if (P->nn >= P->ncap) {
        P->ncap = P->ncap ? P->ncap * 2 : 32;
        P->nodes = (RNode *)xrealloc(P->nodes, sizeof(RNode) * (size_t)P->ncap);
    }
    nd = &P->nodes[P->nn];
    nd->type = type; nd->a = nd->b = nd->c = 0; nd->kid = nd->next = -1;
    nd->wlo = -2; nd->whi = 0;
    nd->lb[0] = nd->lb[1] = nd->lb[2] = nd->lb[3] = 0;
    return P->nn++;
}
#define RNN(P, i) ((P)->nodes[i])
static int rp_set_new(RParse *P)
{
    if (P->nsets >= P->setcap) {
        P->setcap = P->setcap ? P->setcap * 2 : 8;
        P->sets = (RSet *)xrealloc(P->sets, sizeof(RSet) * (size_t)P->setcap);
    }
    memset(&P->sets[P->nsets], 0, sizeof(RSet));
    return P->nsets++;
}
static void rp_rng_add(RParse *P, long lo, long hi)
{
    if (P->nrng >= P->rngcap) {
        P->rngcap = P->rngcap ? P->rngcap * 2 : 16;
        P->rng = (long *)xrealloc(P->rng, sizeof(long) * 2 * (size_t)P->rngcap);
    }
    P->rng[2 * P->nrng] = lo;
    P->rng[2 * P->nrng + 1] = hi;
    P->nrng++;
}
static int rp_lit_cp(RParse *P, long cp)
{
    int ni = rn_new(P, RN_LIT);
    RNN(P, ni).a = re_u8enc(cp, RNN(P, ni).lb);
    RNN(P, ni).b = (P->flags & RF_I) != 0;
    return ni;
}
static int rp_lit_tok(RParse *P, long st, int len)
{
    int ni = rn_new(P, RN_LIT);
    int k;
    for (k = 0; k < len && k < 4; k++) RNN(P, ni).lb[k] = P->t.s[st + k];
    RNN(P, ni).a = len < 4 ? len : 4;
    RNN(P, ni).b = (P->flags & RF_I) != 0;
    return ni;
}
static long rp_cp_of(RParse *P, long st, int len)
{
    int l;
    (void)len;
    return re_u8dec(P->t.s, st, st + len, &l);
}

/* widths in characters, -1 = unbounded */
static long re_wadd(long a, long b)
{
    if (a < 0 || b < 0) return -1;
    if (a + b > 1000000000L) return -1;
    return a + b;
}
static long re_wmul(long a, long k)
{
    if (k == 0 || a == 0) return 0;
    if (a < 0) return -1;
    if (a > 1000000000L / k) return -1;
    return a * k;
}
static void rn_width(RParse *P, int ni, long *lo, long *hi)
{
    RNode *nd = &RNN(P, ni);
    long l = 0, h = 0, l2, h2;
    int k;
    if (nd->wlo != -2) { *lo = nd->wlo; *hi = nd->whi; return; }
    switch (nd->type) {
    case RN_LIT: case RN_ANY: case RN_SET: l = h = 1; break;
    case RN_CAT:
        for (k = nd->kid; k >= 0; k = RNN(P, k).next) {
            rn_width(P, k, &l2, &h2);
            l = re_wadd(l, l2); h = re_wadd(h, h2);
        }
        break;
    case RN_ALT: {
        int first = 1;
        for (k = nd->kid; k >= 0; k = RNN(P, k).next) {
            rn_width(P, k, &l2, &h2);
            if (first) { l = l2; h = h2; first = 0; }
            else {
                if (l2 < l) l = l2;
                if (h >= 0 && (h2 < 0 || h2 > h)) h = h2;
            }
        }
        break; }
    case RN_GROUP: case RN_ATOMIC: rn_width(P, nd->kid, &l, &h); break;
    case RN_REP: {
        long mn = nd->a, mx = nd->b;
        rn_width(P, nd->kid, &l2, &h2);
        l = re_wmul(l2, mn);
        if (mx < 0) h = (h2 == 0) ? 0 : -1;
        else h = h2 < 0 ? -1 : re_wmul(h2, mx);
        break; }
    case RN_BREF:
        if (nd->a <= P->ng && P->gclosed[nd->a]) { l = P->gwlo[nd->a]; h = P->gwhi[nd->a]; }
        else { l = 0; h = -1; }
        break;
    case RN_COND:
        rn_width(P, nd->kid, &l, &h);
        if (nd->b >= 0) {
            rn_width(P, (int)nd->b, &l2, &h2);
            if (l2 < l) l = l2;
            if (h >= 0 && (h2 < 0 || h2 > h)) h = h2;
        } else {
            l = 0;
        }
        break;
    default: l = h = 0; break;
    }
    nd = &RNN(P, ni);
    nd->wlo = l; nd->whi = h;
    *lo = l; *hi = h;
}

static int rp_parse_sub(RParse *P, int verbose, int nested);

/* ---- character sets --------------------------------------------------- */
typedef struct RSetB {
    unsigned char bm[16];
    long hif;
    long rstart;
} RSetB;
static void rb_add_range(RParse *P, RSetB *b, long lo, long hi)
{
    long c;
    if (lo < 128) {
        long e = hi < 127 ? hi : 127;
        for (c = lo; c <= e; c++) BM_SET(b->bm, c);
    }
    if (hi >= 128) rp_rng_add(P, lo < 128 ? 128 : lo, hi);
}
static void rb_add_cat(RSetB *b, int cat, int ascii)
{
    int c;
    const char *sp = ascii ? " \t\n\r\f\v" : " \t\n\r\f\v\x1c\x1d\x1e\x1f";
    for (c = 0; c < 128; c++) {
        int in;
        switch (cat) {
        case 'd': in = ch_digit[c]; break;
        case 'D': in = !ch_digit[c]; break;
        case 'w': in = ch_alnum[c]; break;
        case 'W': in = !ch_alnum[c]; break;
        case 's': in = c != 0 && strchr(sp, c) != NULL; break;
        default: in = !(c != 0 && strchr(sp, c) != NULL); break;
        }
        if (in) BM_SET(b->bm, c);
    }
    switch (cat) {
    case 'D': b->hif |= HF_ALL; break;
    case 'w': if (!ascii) b->hif |= HF_WORD; break;
    case 'W': b->hif |= ascii ? HF_ALL : HF_NONWORD; break;
    case 's': if (!ascii) b->hif |= HF_SPACE; break;
    case 'S': b->hif |= ascii ? HF_ALL : HF_NONSPACE; break;
    default: break;
    }
}
static int rb_finish(RParse *P, RSetB *b, int neg, int icase)
{
    int si = rp_set_new(P);
    RSet *st = &P->sets[si];
    int c;
    if (icase) {
        const char *lp;
        for (lp = "abcdefghijklmnopqrstuvwxyz"; *lp; lp++) {
            int u;
            c = (unsigned char)*lp;
            u = to_up[c];
            if (u == c) continue;
            if (BM_HAS(b->bm, c) || BM_HAS(b->bm, u)) { BM_SET(b->bm, c); BM_SET(b->bm, u); }
        }
    }
    memcpy(st->bm, b->bm, 16);
    if (neg) for (c = 0; c < 16; c++) st->bm[c] = (unsigned char)~st->bm[c];
    st->hif = b->hif;
    st->neg = neg != 0;
    st->roff = b->rstart;
    st->nrng = P->nrng - b->rstart;
    st->pure = (st->hif == 0 && st->nrng == 0 && !st->neg);
    return si;
}

typedef struct RCode { int islit; long cp; int cat; } RCode;

/* the escape token starts at st and has len bytes; returns after consuming extra characters */
static long rp_hexval(RParse *P, long ndig, long *tot)
{
    long v = 0, got = 0;
    while (got < ndig && P->t.nlen == 1 && ch_hex[P->t.s[P->t.nstart]]) {
        int c = P->t.s[P->t.nstart];
        int d = digit_val(c);
        v = v * 16 + d;
        got++;
        rt_advance(&P->t, P->patv);
    }
    *tot = got;
    return v;
}
static int rp_simple_escape(int c, long *cp)
{
    switch (c) {
    case 'a': *cp = 7; return 1;
    case 'b': *cp = 8; return 1;
    case 'f': *cp = 12; return 1;
    case 'n': *cp = 10; return 1;
    case 'r': *cp = 13; return 1;
    case 't': *cp = 9; return 1;
    case 'v': *cp = 11; return 1;
    case '\\': *cp = 92; return 1;
    default: return 0;
    }
}
static void rp_bad_escape(RParse *P, long st, long len)
{
    char buf[80];
    long k = len > 30 ? 30 : len;
    char e[40];
    memcpy(e, P->t.s + st, (size_t)k);
    e[k] = 0;
    sprintf(buf, "bad escape %s", e);
    rp_fail(P, buf, len);
}
/* x/u/U escapes shared by class and top level; escape tok at st,len.  Returns the code point */
static long rp_hex_escape(RParse *P, long st, int c)
{
    long ndig = c == 'x' ? 2 : c == 'u' ? 4 : 8, got, v;
    char buf[80];
    long total;
    v = rp_hexval(P, ndig, &got);
    total = 2 + got;
    if (got != ndig) {
        char e[24];
        memcpy(e, P->t.s + st, (size_t)total);
        e[total] = 0;
        sprintf(buf, "incomplete escape %s", e);
        rp_fail(P, buf, total);
    }
    if (c == 'U' && v > 0x10FFFFL) {
        char e[24];
        memcpy(e, P->t.s + st, (size_t)total);
        e[total] = 0;
        sprintf(buf, "bad escape %s", e);
        rp_fail(P, buf, total);
    }
    return v;
}
static int rp_getuntil(RParse *P, int term, const char *name, long *off, long *rlen);
static void rp_checkgroupname(RParse *P, long off, long len, long extra);
static void rp_charname(RParse *P)
{
    long off, len;
    Value r;
    if (!rt_match(&P->t, P->patv, '{')) rp_fail(P, "missing {", 0);
    rp_getuntil(P, '}', "character name", &off, &len);
    r = re_repr_range(P->t.s, off, len);
    rp_failf(P, len + 4, "undefined character name %s", AS_STR(r)->s, -1);
}
static void rp_class_escape(RParse *P, long st, int len, RCode *out)
{
    int c = len >= 2 ? P->t.s[st + 1] : 0;
    long cp;
    out->islit = 1; out->cat = 0; out->cp = 0;
    if (len == 2 && rp_simple_escape(c, &cp)) { out->cp = cp; return; }
    if (len == 2 && c != 0 && strchr("dDsSwW", c)) { out->islit = 0; out->cat = c; return; }
    if (len == 2 && (c == 'x' || c == 'u' || c == 'U')) { out->cp = rp_hex_escape(P, st, c);
        return;
    }
    if (len == 2 && c == 'N') {
        rp_charname(P);
    }
    if (len == 2 && re_isoctc(c)) {
        long v = c - '0', got = 0;
        while (got < 2 && P->t.nlen == 1 && re_isoctc(P->t.s[P->t.nstart])) {
            v = v * 8 + (P->t.s[P->t.nstart] - '0');
            got++;
            rt_advance(&P->t, P->patv);
        }
        if (v > 0377) {
            char e[16], buf[80];
            memcpy(e, P->t.s + st, (size_t)(2 + got));
            e[2 + got] = 0;
            sprintf(buf, "octal escape value %s outside of range 0-0o377", e);
            rp_fail(P, buf, 2 + got);
        }
        out->cp = v;
        return;
    }
    if (len == 2 && re_isdigitc(c)) rp_bad_escape(P, st, len);
    if (c < 128 && ch_alpha[c] && c != '_') rp_bad_escape(P, st, len);
    if (len >= 2) { out->cp = rp_cp_of(P, st + 1, len - 1); return; }
    rp_bad_escape(P, st, len);
}

static int rp_class(RParse *P)
{
    RSetB b;
    long here = P->t.nstart - 1;
    int negate, nitems = 0;
    long st, st2;
    int len, len2;
    int ascii = (P->flags & RF_A) != 0;
    memset(&b, 0, sizeof(b));
    b.rstart = P->nrng;
    negate = rt_match(&P->t, P->patv, '^');
    for (;;) {
        RCode c1, c2;
        if (!rt_get(&P->t, P->patv, &st, &len))
            rp_fail(P, "unterminated character set", P->t.nstart - here);
        if (len == 1 && P->t.s[st] == ']' && nitems > 0) break;
        if (P->t.s[st] == '\\') {
            rp_class_escape(P, st, len, &c1);
        } else {
            c1.islit = 1; c1.cat = 0; c1.cp = rp_cp_of(P, st, len);
        }
        nitems++;
        if (rt_match(&P->t, P->patv, '-')) {
            if (!rt_get(&P->t, P->patv, &st2, &len2))
                rp_fail(P, "unterminated character set", P->t.nstart - here);
            if (len2 == 1 && P->t.s[st2] == ']') {
                if (c1.islit) rb_add_range(P, &b, c1.cp, c1.cp); else rb_add_cat(&b, c1.cat, ascii);
                rb_add_range(P, &b, '-', '-');
                break;
            }
            if (P->t.s[st2] == '\\') {
                rp_class_escape(P, st2, len2, &c2);
            } else {
                c2.islit = 1; c2.cat = 0; c2.cp = rp_cp_of(P, st2, len2);
            }
            if (!c1.islit || !c2.islit || c2.cp < c1.cp) {
                char buf[160], t1[40], t2[40];
                long k1 = len > 30 ? 30 : len, k2 = len2 > 30 ? 30 : len2;
                memcpy(t1, P->t.s + st, (size_t)k1); t1[k1] = 0;
                memcpy(t2, P->t.s + st2, (size_t)k2); t2[k2] = 0;
                sprintf(buf, "bad character range %s-%s", t1, t2);
                rp_fail(P, buf, len + 1 + len2);
            }
            rb_add_range(P, &b, c1.cp, c2.cp);
        } else {
            if (c1.islit) rb_add_range(P, &b, c1.cp, c1.cp); else rb_add_cat(&b, c1.cat, ascii);
        }
    }
    {
        int si = rb_finish(P, &b, negate, (P->flags & RF_I) != 0);
        int ni = rn_new(P, RN_SET);
        RNN(P, ni).a = si;
        return ni;
    }
}

/* ---- escapes at top level --------------------------------------------- */
static int rp_checkgroup(RParse *P, long gid)
{
    return gid <= P->ng && P->gclosed[gid];
}
static void rp_check_lookbehind_group(RParse *P, long gid)
{
    if (P->lbg >= 0) {
        if (!rp_checkgroup(P, gid)) rp_fail(P, "cannot refer to an open group", 0);
        if (gid >= P->lbg)
            rp_fail(P, "cannot refer to group defined in the same lookbehind subpattern", 0);
    }
}
static int rp_at_node(RParse *P, int kind)
{
    int ni = rn_new(P, RN_AT);
    RNN(P, ni).a = kind;
    return ni;
}
static int rp_escape(RParse *P, long st, int len)
{
    int c = len >= 2 ? P->t.s[st + 1] : 0;
    long cp;
    int ascii = (P->flags & RF_A) != 0;
    if (len == 2 && c == 'A') return rp_at_node(P, RA_BOS);
    if (len == 2 && c == 'b') return rp_at_node(P, ascii ? RA_WBA : RA_WB);
    if (len == 2 && c == 'B') return rp_at_node(P, ascii ? RA_NWBA : RA_NWB);
    if (len == 2 && c == 'Z') return rp_at_node(P, RA_EOS);
    if (len == 2 && strchr("dDsSwW", c) && c != 0) {
        RSetB b;
        int si, ni;
        memset(&b, 0, sizeof(b));
        b.rstart = P->nrng;
        rb_add_cat(&b, c, ascii);
        si = rb_finish(P, &b, 0, 0);
        ni = rn_new(P, RN_SET);
        RNN(P, ni).a = si;
        return ni;
    }
    if (len == 2 && rp_simple_escape(c, &cp)) return rp_lit_cp(P, cp);
    if (len == 2 && (c == 'x' || c == 'u' || c == 'U'))
        return rp_lit_cp(P, rp_hex_escape(P, st, c));
    if (len == 2 && c == 'N') {
        rp_charname(P);
    }
    if (len == 2 && c == '0') {
        long v = 0, got = 0;
        while (got < 2 && P->t.nlen == 1 && re_isoctc(P->t.s[P->t.nstart])) {
            v = v * 8 + (P->t.s[P->t.nstart] - '0');
            got++;
            rt_advance(&P->t, P->patv);
        }
        return rp_lit_cp(P, v);
    }
    if (len == 2 && re_isdigitc(c)) {
        char esc[8];
        int el = 2;
        long group;
        esc[0] = '\\'; esc[1] = (char)c;
        if (P->t.nlen == 1 && re_isdigitc(P->t.s[P->t.nstart])) {
            esc[el++] = (char)P->t.s[P->t.nstart];
            rt_advance(&P->t, P->patv);
            if (re_isoctc(esc[1]) && re_isoctc(esc[2]) && P->t.nlen == 1 &&
                re_isoctc(P->t.s[P->t.nstart])) {
                long v;
                esc[el++] = (char)P->t.s[P->t.nstart];
                rt_advance(&P->t, P->patv);
                esc[el] = 0;
                v = (esc[1] - '0') * 64 + (esc[2] - '0') * 8 + (esc[3] - '0');
                if (v > 0377) {
                    char buf[80];
                    sprintf(buf, "octal escape value %s outside of range 0-0o377", esc);
                    rp_fail(P, buf, el);
                }
                return rp_lit_cp(P, v);
            }
        }
        esc[el] = 0;
        group = atol(esc + 1);
        if (group <= P->ng) {
            int ni;
            if (!rp_checkgroup(P, group)) rp_fail(P, "cannot refer to an open group", el);
            rp_check_lookbehind_group(P, group);
            ni = rn_new(P, RN_BREF);
            RNN(P, ni).a = group;
            RNN(P, ni).b = (P->flags & RF_I) != 0;
            return ni;
        }
        {
            char buf[80];
            sprintf(buf, "invalid group reference %ld", group);
            rp_fail(P, buf, el - 1);
        }
    }
    if (len >= 2) {
        if (len == 2 && c < 128 && ch_alpha[c] && c != '_') rp_bad_escape(P, st, len);
        return rp_lit_tok(P, st + 1, len - 1);
    }
    rp_bad_escape(P, st, len);
    return 0;
}

/* ---- groups ----------------------------------------------------------- */
static int rp_getuntil(RParse *P, int term, const char *name, long *off, long *rlen)
{
    long start = P->t.nstart;
    long total = 0;
    (void)start;
    *off = P->t.nstart;
    for (;;) {
        long st;
        int len;
        char buf[100];
        if (!rt_get(&P->t, P->patv, &st, &len)) {
            if (total == 0) {
                sprintf(buf, "missing %s", name);
                rp_fail(P, buf, 0);
            }
            sprintf(buf, "missing %c, unterminated name", term);
            rp_fail(P, buf, total);
        }
        if (len == 1 && P->t.s[st] == term) {
            if (total == 0) {
                sprintf(buf, "missing %s", name);
                rp_fail(P, buf, 1);
            }
            break;
        }
        total += len;
    }
    *rlen = total;
    return 1;
}
static void rp_checkgroupname(RParse *P, long off, long len, long extra)
{
    if (!re_isident(P->t.s + off, len)) {
        Value r = re_repr_range(P->t.s, off, len);
        rp_failf(P, len + extra, "bad character in group name %s", AS_STR(r)->s, -1);
    }
}
static int rp_find_name(RParse *P, long off, long len)
{
    int i;
    for (i = 0; i < P->nnm; i++)
        if (P->nm_len[i] == len && memcmp(P->t.s + P->nm_off[i], P->t.s + off, (size_t)len) == 0)
            return P->nm_gid[i];
    return -1;
}
static void rp_grow_groups(RParse *P)
{
    if (P->ng + 2 > P->gcap) {
        P->gcap = P->gcap ? P->gcap * 2 : 16;
        P->gclosed = (unsigned char *)xrealloc(P->gclosed, (size_t)P->gcap);
        P->gwlo = (long *)xrealloc(P->gwlo, sizeof(long) * (size_t)P->gcap);
        P->gwhi = (long *)xrealloc(P->gwhi, sizeof(long) * (size_t)P->gcap);
    }
}
static int rp_opengroup(RParse *P, long noff, long nlen, int hasname)
{
    int gid;
    rp_grow_groups(P);
    gid = ++P->ng;
    P->gclosed[gid] = 0;
    P->gwlo[gid] = P->gwhi[gid] = 0;
    if (hasname) {
        int o = rp_find_name(P, noff, nlen);
        if (o >= 0) {
            Value r = re_repr_range(P->t.s, noff, nlen);
            char buf[500];
            sprintf(buf, "redefinition of group name %.200s as group %d; was group %d",
                    AS_STR(r)->s, gid, o);
            rp_fail(P, buf, nlen + 1);
        }
        if (P->nnm >= P->nmcap) {
            P->nmcap = P->nmcap ? P->nmcap * 2 : 8;
            P->nm_off = (long *)xrealloc(P->nm_off, sizeof(long) * (size_t)P->nmcap);
            P->nm_len = (long *)xrealloc(P->nm_len, sizeof(long) * (size_t)P->nmcap);
            P->nm_gid = (int *)xrealloc(P->nm_gid, sizeof(int) * (size_t)P->nmcap);
        }
        P->nm_off[P->nnm] = noff; P->nm_len[P->nnm] = nlen; P->nm_gid[P->nnm] = gid;
        P->nnm++;
    }
    return gid;
}
static long rp_combine_flags(long flags, long add, long del)
{
    if (add & RE_TYPEF) flags &= ~RE_TYPEF;
    return (flags | add) & ~del;
}
static const char *RE_FLAGCH = "iLmsxau";
static long re_flagbit(int c)
{
    switch (c) {
    case 'i': return RF_I;
    case 'L': return RF_L;
    case 'm': return RF_M;
    case 's': return RF_S;
    case 'x': return RF_X;
    case 'a': return RF_A;
    case 'u': return RF_U;
    default: return 0;
    }
}
static int re_isflagch(int c)
{
    return c > 0 && c < 128 && strchr(RE_FLAGCH, c) != NULL;
}
/* port of _parse_flags; ch is the first flag character (already consumed).
   returns 1 for global flags (state updated), 0 for scoped flags */
static int rp_parse_flags(RParse *P, int ch, long *addp, long *delp)
{
    long add = 0, del = 0, st;
    int len;
    int c = ch;
    if (c != '-') {
        for (;;) {
            long flag = re_flagbit(c);
            if (c == 'L') rp_fail(P, "bad inline flags: cannot use 'L' flag with a str pattern", 0);
            add |= flag;
            if ((flag & RE_TYPEF) && (add & RE_TYPEF) != flag)
                rp_fail(P, "bad inline flags: flags 'a', 'u' and 'L' are incompatible", 0);
            if (!rt_get(&P->t, P->patv, &st, &len)) rp_fail(P, "missing -, : or )", 0);
            c = len == 1 ? P->t.s[st] : 0;
            if (len == 1 && (c == ')' || c == '-' || c == ':')) break;
            if (!re_isflagch(c) || len != 1) {
                int alpha = len == 1 && c < 128 && ch_alpha[c] && c != '_';
                rp_fail(P, alpha ? "unknown flag" : "missing -, : or )", len);
            }
        }
    }
    if (c == ')') {
        P->gflags |= add;
        P->flags |= add;
        return 1;
    }
    if (c == '-') {
        if (!rt_get(&P->t, P->patv, &st, &len)) rp_fail(P, "missing flag", 0);
        c = len == 1 ? P->t.s[st] : 0;
        if (!re_isflagch(c) || len != 1) {
            int alpha = len == 1 && c < 128 && ch_alpha[c] && c != '_';
            rp_fail(P, alpha ? "unknown flag" : "missing flag", len);
        }
        for (;;) {
            long flag = re_flagbit(c);
            if (flag & RE_TYPEF)
                rp_fail(P, "bad inline flags: cannot turn off flags 'a', 'u' and 'L'", 0);
            del |= flag;
            if (!rt_get(&P->t, P->patv, &st, &len)) rp_fail(P, "missing :", 0);
            c = len == 1 ? P->t.s[st] : 0;
            if (len == 1 && c == ':') break;
            if (!re_isflagch(c) || len != 1) {
                int alpha = len == 1 && c < 128 && ch_alpha[c] && c != '_';
                rp_fail(P, alpha ? "unknown flag" : "missing :", len);
            }
        }
    }
    if (add & del) rp_fail(P, "bad inline flags: flag turned on and off", 1);
    *addp = add; *delp = del;
    return 0;
}

static void rp_cr_add(RParse *P, int gid, long pos)
{
    int i;
    for (i = 0; i < P->ncr; i++) if (P->cr_gid[i] == gid) return;
    if (P->ncr >= P->crcap) {
        P->crcap = P->crcap ? P->crcap * 2 : 8;
        P->cr_gid = (int *)xrealloc(P->cr_gid, sizeof(int) * (size_t)P->crcap);
        P->cr_pos = (long *)xrealloc(P->cr_pos, sizeof(long) * (size_t)P->crcap);
    }
    P->cr_gid[P->ncr] = gid; P->cr_pos[P->ncr] = pos; P->ncr++;
}

static int rp_parse(RParse *P, int verbose, int nested, int first);

/* parse a group; the '(' has been consumed.  Returns a node or -1 when the construct yields
   nothing (comment, global flags) */
static int rp_group(RParse *P, int verbose, int nested, int first, int haveitems)
{
    long start = P->t.nstart - 1;
    int capture = 1, atomic = 0, hasname = 0, gid = 0, ni, body;
    long noff = 0, nlen = 0;
    long add = 0, del = 0, saved_flags;
    int subverb;
    if (rt_match(&P->t, P->patv, '?')) {
        long st;
        int len, ch;
        if (!rt_get(&P->t, P->patv, &st, &len)) rp_fail(P, "unexpected end of pattern", 0);
        ch = len == 1 ? P->t.s[st] : 0;
        if (ch == 'P') {
            if (rt_match(&P->t, P->patv, '<')) {
                rp_getuntil(P, '>', "group name", &noff, &nlen);
                rp_checkgroupname(P, noff, nlen, 1);
                hasname = 1;
            } else if (rt_match(&P->t, P->patv, '=')) {
                int g;
                rp_getuntil(P, ')', "group name", &noff, &nlen);
                rp_checkgroupname(P, noff, nlen, 1);
                g = rp_find_name(P, noff, nlen);
                if (g < 0) {
                    Value r = re_repr_range(P->t.s, noff, nlen);
                    rp_failf(P, nlen + 1, "unknown group name %s", AS_STR(r)->s, -1);
                }
                if (!rp_checkgroup(P, g)) rp_fail(P, "cannot refer to an open group", nlen + 1);
                rp_check_lookbehind_group(P, g);
                ni = rn_new(P, RN_BREF);
                RNN(P, ni).a = g;
                RNN(P, ni).b = (P->flags & RF_I) != 0;
                return ni;
            } else {
                char buf[60];
                if (!rt_get(&P->t, P->patv, &st, &len)) rp_fail(P, "unexpected end of pattern", 0);
                memcpy(buf, "unknown extension ?P", 20);
                memcpy(buf + 20, P->t.s + st, (size_t)(len > 20 ? 20 : len));
                buf[20 + (len > 20 ? 20 : len)] = 0;
                rp_fail(P, buf, len + 2);
            }
        } else if (ch == ':') {
            capture = 0;
        } else if (ch == '#') {
            for (;;) {
                if (P->t.nlen == 0)
                    rp_fail(P, "missing ), unterminated comment", P->t.nstart - start);
                rt_get(&P->t, P->patv, &st, &len);
                if (len == 1 && P->t.s[st] == ')') break;
            }
            return -1;
        } else if (ch == '=' || ch == '!' || ch == '<') {
            int behind = 0, neg;
            long savedlbg = P->lbg;
            if (ch == '<') {
                char buf[60];
                if (!rt_get(&P->t, P->patv, &st, &len)) rp_fail(P, "unexpected end of pattern", 0);
                ch = len == 1 ? P->t.s[st] : 0;
                if (ch != '=' && ch != '!') {
                    memcpy(buf, "unknown extension ?<", 20);
                    memcpy(buf + 20, P->t.s + st, (size_t)(len > 20 ? 20 : len));
                    buf[20 + (len > 20 ? 20 : len)] = 0;
                    rp_fail(P, buf, len + 2);
                }
                behind = 1;
                if (P->lbg < 0) P->lbg = P->ng + 1;
            }
            neg = ch == '!';
            body = rp_parse_sub(P, verbose, nested + 1);
            if (behind && savedlbg < 0) P->lbg = -1;
            if (!rt_match(&P->t, P->patv, ')'))
                rp_fail(P, "missing ), unterminated subpattern", P->t.nstart - start);
            if (neg && RNN(P, body).type == RN_EMPTY) return rn_new(P, RN_FAIL);
            ni = rn_new(P, RN_LOOK);
            RNN(P, ni).a = behind;
            RNN(P, ni).b = neg;
            RNN(P, ni).kid = body;
            return ni;
        } else if (ch == '(') {
            int condg, cy, cn = -1;
            long coff, clen;
            rp_getuntil(P, ')', "group name", &coff, &clen);
            {
                long k;
                int alldig = 1;
                for (k = 0; k < clen; k++) if (!re_isdigitc(P->t.s[coff + k])) alldig = 0;
                if (!alldig) {
                    rp_checkgroupname(P, coff, clen, 1);
                    condg = rp_find_name(P, coff, clen);
                    if (condg < 0) {
                        Value r = re_repr_range(P->t.s, coff, clen);
                        rp_failf(P, clen + 1, "unknown group name %s", AS_STR(r)->s, -1);
                    }
                } else {
                    char tmp[24];
                    long vv;
                    memcpy(tmp, P->t.s + coff, (size_t)(clen > 20 ? 20 : clen));
                    tmp[clen > 20 ? 20 : clen] = 0;
                    vv = atol(tmp);
                    if (vv == 0) rp_fail(P, "bad group number", clen + 1);
                    if (vv >= 100000L || clen > 9) {
                        char buf[80];
                        sprintf(buf, "invalid group reference %ld", vv);
                        rp_fail(P, buf, clen + 1);
                    }
                    condg = (int)vv;
                    rp_cr_add(P, condg, P->t.nstart - clen - 1);
                }
            }
            rp_check_lookbehind_group(P, condg);
            cy = rp_parse(P, verbose, nested + 1, 0);
            if (rt_match(&P->t, P->patv, '|')) {
                cn = rp_parse(P, verbose, nested + 1, 0);
                if (RT_NEXT1(&P->t, '|'))
                    rp_fail(P, "conditional backref with more than two branches", 0);
            }
            if (!rt_match(&P->t, P->patv, ')'))
                rp_fail(P, "missing ), unterminated subpattern", P->t.nstart - start);
            ni = rn_new(P, RN_COND);
            RNN(P, ni).a = condg;
            RNN(P, ni).kid = cy;
            RNN(P, ni).b = cn;
            return ni;
        } else if (ch == '>') {
            capture = 0;
            atomic = 1;
        } else if (len == 1 && (re_isflagch(ch) || ch == '-')) {
            if (rp_parse_flags(P, ch, &add, &del)) {
                if (!first || haveitems)
                    rp_fail(P, "global flags not at the start of the expression",
                        P->t.nstart - start);
                return -2;
            }
            capture = 0;
        } else {
            char buf[60];
            memcpy(buf, "unknown extension ?", 19);
            memcpy(buf + 19, P->t.s + st, (size_t)(len > 20 ? 20 : len));
            buf[19 + (len > 20 ? 20 : len)] = 0;
            rp_fail(P, buf, len + 1);
        }
    }
    if (capture) gid = rp_opengroup(P, noff, nlen, hasname);
    subverb = ((verbose || (add & RF_X)) && !(del & RF_X));
    saved_flags = P->flags;
    P->flags = rp_combine_flags(P->flags, add, del);
    body = rp_parse_sub(P, subverb, nested + 1);
    P->flags = saved_flags;
    if (!rt_match(&P->t, P->patv, ')'))
        rp_fail(P, "missing ), unterminated subpattern", P->t.nstart - start);
    if (gid) {
        long lo, hi;
        rn_width(P, body, &lo, &hi);
        P->gclosed[gid] = 1;
        P->gwlo[gid] = lo; P->gwhi[gid] = hi;
    }
    if (atomic) {
        ni = rn_new(P, RN_ATOMIC);
        RNN(P, ni).kid = body;
        return ni;
    }
    ni = rn_new(P, RN_GROUP);
    RNN(P, ni).a = gid;
    RNN(P, ni).kid = body;
    return ni;
}

static int rp_parse(RParse *P, int verbose, int nested, int first)
{
    int head = -1, tail = -1, prev = -1;
    long st;
    int len;
    long ofs;
    if (++P->depth > RE_MAXDEPTH)
        re_throw_cls(cls_RecursionError, "maximum recursion depth exceeded during compilation");
    for (;;) {
        int ni = -1, c;
        if (P->t.nlen == 0) break;
        c = P->t.nlen == 1 ? P->t.s[P->t.nstart] : 0;
        if (c == '|' || c == ')') break;
        rt_get(&P->t, P->patv, &st, &len);
        if (verbose) {
            if (len == 1 && (P->t.s[st] == ' ' || P->t.s[st] == '\t' || P->t.s[st] == '\n' ||
                             P->t.s[st] == '\r' || P->t.s[st] == '\v' || P->t.s[st] == '\f'))
                continue;
            if (len == 1 && P->t.s[st] == '#') {
                for (;;) {
                    long s2;
                    int l2;
                    if (!rt_get(&P->t, P->patv, &s2, &l2)) break;
                    if (l2 == 1 && P->t.s[s2] == '\n') break;
                }
                continue;
            }
        }
        c = P->t.s[st];
        if (c == '\\') {
            ni = rp_escape(P, st, len);
        } else if (len > 1 || strchr(".[{()*+?^$|", c) == NULL || c == 0) {
            ni = rp_lit_tok(P, st, len);
        } else if (c == '[') {
            ni = rp_class(P);
        } else if (c == '*' || c == '+' || c == '?' || c == '{') {
            long here = P->t.nstart, mn, mx;
            int mode = 0, item;
            if (c == '?') { mn = 0; mx = 1; }
            else if (c == '*') { mn = 0; mx = -1; }
            else if (c == '+') { mn = 1; mx = -1; }
            else {
                char lo[16], hi[16];
                int nlo = 0, nhi = 0;
                memset(lo, 0, sizeof(lo));
                memset(hi, 0, sizeof(hi));
                if (RT_NEXT1(&P->t, '}')) {
                    ni = rp_lit_tok(P, st, len);
                    goto append;
                }
                mn = 0; mx = -1;
                while (P->t.nlen == 1 && re_isdigitc(P->t.s[P->t.nstart])) {
                    if (nlo < 15) lo[nlo] = (char)P->t.s[P->t.nstart];
                    nlo++;
                    rt_advance(&P->t, P->patv);
                }
                if (rt_match(&P->t, P->patv, ',')) {
                    while (P->t.nlen == 1 && re_isdigitc(P->t.s[P->t.nstart])) {
                        if (nhi < 15) hi[nhi] = (char)P->t.s[P->t.nstart];
                        nhi++;
                        rt_advance(&P->t, P->patv);
                    }
                } else {
                    memcpy(hi, lo, 16);
                    nhi = nlo;
                }
                if (!rt_match(&P->t, P->patv, '}')) {
                    ni = rp_lit_tok(P, st, len);
                    rt_seek(&P->t, P->patv, here);
                    goto append;
                }
                if (nlo > 0) {
                    if (nlo > 10)
                        re_throw_cls(cls_OverflowError, "the repetition number is too large");
                    lo[nlo] = 0;
                    mn = atol(lo);
                    if (mn >= RE_MAXREP)
                        re_throw_cls(cls_OverflowError, "the repetition number is too large");
                }
                if (nhi > 0) {
                    if (nhi > 10)
                        re_throw_cls(cls_OverflowError, "the repetition number is too large");
                    hi[nhi] = 0;
                    mx = atol(hi);
                    if (mx >= RE_MAXREP)
                        re_throw_cls(cls_OverflowError, "the repetition number is too large");
                    if (mx < mn)
                        rp_fail(P, "min repeat greater than max repeat", P->t.nstart - here);
                }
            }
            ofs = P->t.nstart - here + len;
            item = tail;
            if (item < 0 || RNN(P, item).type == RN_AT) rp_fail(P, "nothing to repeat", ofs);
            if (RNN(P, item).type == RN_REP) rp_fail(P, "multiple repeat", ofs);
            if (rt_match(&P->t, P->patv, '?')) mode = 1;
            else if (rt_match(&P->t, P->patv, '+')) mode = 2;
            ni = rn_new(P, RN_REP);
            RNN(P, ni).a = mn; RNN(P, ni).b = mx; RNN(P, ni).c = mode;
            RNN(P, ni).kid = item;
            RNN(P, item).next = -1;
            if (prev >= 0) RNN(P, prev).next = ni; else head = ni;
            tail = ni;
            continue;
        } else if (c == '.') {
            ni = rn_new(P, RN_ANY);
            RNN(P, ni).a = (P->flags & RF_S) != 0;
        } else if (c == '(') {
            ni = rp_group(P, verbose, nested, first, head >= 0);
            if (ni == -1) continue;
            if (ni == -2) {
                verbose = (P->gflags & RF_X) != 0;
                continue;
            }
        } else if (c == '^') {
            ni = rp_at_node(P, (P->flags & RF_M) ? RA_BOL : RA_BOS);
        } else if (c == '$') {
            ni = rp_at_node(P, (P->flags & RF_M) ? RA_EOLM : RA_EOL);
        } else {
            ni = rp_lit_tok(P, st, len);
        }
      append:
        if (head < 0) head = ni; else RNN(P, tail).next = ni;
        prev = tail;
        tail = ni;
    }
    P->depth--;
    if (head < 0) return rn_new(P, RN_EMPTY);
    if (RNN(P, head).next < 0) return head;
    {
        int cn = rn_new(P, RN_CAT);
        RNN(P, cn).kid = head;
        return cn;
    }
}

static int rp_parse_sub(RParse *P, int verbose, int nested)
{
    int first_item = 1;
    int head = -1, tail = -1, b;
    if (P->depth > RE_MAXDEPTH)
        re_throw_cls(cls_RecursionError, "maximum recursion depth exceeded during compilation");
    for (;;) {
        b = rp_parse(P, verbose, nested + 1, !nested && first_item);
        first_item = 0;
        if (head < 0) head = b; else RNN(P, tail).next = b;
        tail = b;
        if (!rt_match(&P->t, P->patv, '|')) break;
        if (!nested) verbose = (P->gflags & RF_X) != 0;
    }
    if (RNN(P, head).next < 0) return head;
    {
        int an = rn_new(P, RN_ALT);
        RNN(P, an).kid = head;
        return an;
    }
}

/* ---- compiler: AST -> instruction list -------------------------------- */
static long rc_emit(RParse *P, long op, long a, long b, long c, long d, long e)
{
    RI *ip;
    if (P->nins >= P->inscap) {
        P->inscap = P->inscap ? P->inscap * 2 : 64;
        P->ins = (RI *)xrealloc(P->ins, sizeof(RI) * (size_t)P->inscap);
    }
    ip = &P->ins[P->nins];
    ip->op = op; ip->a = a; ip->b = b; ip->c = c; ip->d = d; ip->e = e;
    return P->nins++;
}
static long rc_newslot(RParse *P)
{
    return P->nslots++;
}
/* single-character matcher: kind 0 byte, 1 any but newline, 2 any, 3 set */
static int rc_single(RParse *P, int ni, long *kind, long *opnd)
{
    RNode *nd = &RNN(P, ni);
    switch (nd->type) {
    case RN_LIT:
        if (nd->a != 1) return 0;
        if (nd->b && ch_alpha[nd->lb[0]] && nd->lb[0] != '_') {
            RSetB b;
            int si;
            memset(&b, 0, sizeof(b));
            b.rstart = P->nrng;
            BM_SET(b.bm, nd->lb[0]);
            si = rb_finish(P, &b, 0, 1);
            *kind = 3; *opnd = si;
            return 1;
        }
        *kind = 0; *opnd = nd->lb[0];
        return 1;
    case RN_ANY: *kind = nd->a ? 2 : 1; *opnd = 0; return 1;
    case RN_SET: *kind = 3; *opnd = nd->a; return 1;
    case RN_GROUP:
        if (nd->a == 0) return rc_single(P, nd->kid, kind, opnd);
        return 0;
    default: return 0;
    }
}
static void rc_node(RParse *P, int ni);
static void rc_chain(RParse *P, int head, int single)
{
    int i = head;
    while (i >= 0) {
        if (RNN(P, i).type == RN_LIT) {
            int j = i;
            long ic = RNN(P, i).b, start = P->nlit, len = 0;
            int anyalpha = 0;
            while (j >= 0 && RNN(P, j).type == RN_LIT && RNN(P, j).b == ic) {
                long k;
                RNode *nd = &RNN(P, j);
                if (P->nlit + nd->a >= P->litcap) {
                    P->litcap = P->litcap ? P->litcap * 2 + nd->a : 64;
                    P->lit = (unsigned char *)xrealloc(P->lit, (size_t)P->litcap);
                }
                for (k = 0; k < nd->a; k++) {
                    unsigned c = nd->lb[k];
                    if (ic && c < 128 && ch_alpha[c] && c != '_') anyalpha = 1;
                    P->lit[P->nlit++] = (unsigned char)(ic ? to_low[c] : c);
                }
                len += nd->a;
                j = nd->next;
                if (single) break;
            }
            rc_emit(P, (ic && anyalpha) ? RO_LITI : RO_LIT, start, len, 0, 0, 0);
            i = j;
            if (single) break;
            continue;
        }
        rc_node(P, i);
        if (single) break;
        i = RNN(P, i).next;
    }
}
static void rc_rep(RParse *P, int ni)
{
    RNode *nd = &RNN(P, ni);
    long mn = nd->a, mx = nd->b, mode = nd->c, kind, opnd, lo, hi;
    int body = nd->kid;
    long sp, ps, canempty, end, L, slot, lk;
    if (mx == 0) return;
    if (rc_single(P, body, &kind, &opnd)) {
        if (mn == 1 && mx == 1) { rc_node(P, body); return; }
        rc_emit(P, RO_REP1, kind, opnd, mn, mx, mode);
        return;
    }
    if (mn == 1 && mx == 1) { rc_node(P, body); return; }
    if (mode == 2) {
        int at = rn_new(P, RN_ATOMIC);
        lk = rc_emit(P, RO_LOOK, 2, 0, 0, 0, 0);
        RNN(P, at).kid = body;
        RNN(P, ni).kid = at;
        RNN(P, ni).c = 0;
        rc_rep(P, ni);
        RNN(P, ni).c = 2;
        RNN(P, ni).kid = body;
        rc_emit(P, RO_LOOKEND, 0, 0, 0, 0, 0);
        P->ins[lk].d = P->nins;
        return;
    }
    rn_width(P, body, &lo, &hi);
    canempty = lo == 0;
    if (mn == 0 && mx == 1) {
        sp = rc_emit(P, RO_SPLIT, 0, 0, 0, 0, 0);
        rc_node(P, body);
        end = P->nins;
        if (mode == 0) { P->ins[sp].a = sp + 1; P->ins[sp].b = end; }
        else { P->ins[sp].a = end; P->ins[sp].b = sp + 1; }
        return;
    }
    if (mx < 0 && mn == 0) {
        L = rc_emit(P, RO_SPLIT, 0, 0, 0, 0, 0);
        ps = -1;
        if (canempty) { ps = rc_newslot(P); rc_emit(P, RO_SAVE, ps, 0, 0, 0, 0); }
        rc_node(P, body);
        sp = -1;
        if (canempty) sp = rc_emit(P, RO_CHKEMPTY, ps, 0, 0, 0, 0);
        rc_emit(P, RO_JMP, L, 0, 0, 0, 0);
        end = P->nins;
        if (mode == 0) { P->ins[L].a = L + 1; P->ins[L].b = end; }
        else { P->ins[L].a = end; P->ins[L].b = L + 1; }
        if (sp >= 0) P->ins[sp].b = end;
        return;
    }
    if (mx < 0 && mn == 1) {
        L = P->nins;
        ps = -1;
        if (canempty) { ps = rc_newslot(P); rc_emit(P, RO_SAVE, ps, 0, 0, 0, 0); }
        rc_node(P, body);
        sp = -1;
        if (canempty) sp = rc_emit(P, RO_CHKEMPTY, ps, 0, 0, 0, 0);
        lk = rc_emit(P, RO_SPLIT, 0, 0, 0, 0, 0);
        end = P->nins;
        if (mode == 0) { P->ins[lk].a = L; P->ins[lk].b = end; }
        else { P->ins[lk].a = end; P->ins[lk].b = L; }
        if (sp >= 0) P->ins[sp].b = end;
        return;
    }
    /* counted loop */
    slot = rc_newslot(P);
    ps = canempty ? rc_newslot(P) : -1;
    rc_emit(P, RO_CNTSET, slot, 0, 0, 0, 0);
    L = rc_emit(P, RO_LOOP, slot, mn, mx, mode == 0, 0);
    rc_emit(P, RO_ENTER, slot, ps, 0, 0, 0);
    rc_node(P, body);
    rc_emit(P, RO_ENDITER, slot, ps, L, 0, mn);
    end = P->nins;
    P->ins[L].e = end;
    P->ins[end - 1].d = end;
}
static void rc_node(RParse *P, int ni)
{
    RNode *nd = &RNN(P, ni);
    long k, sp, lk;
    switch (nd->type) {
    case RN_EMPTY: break;
    case RN_LIT: rc_chain(P, ni, 1); break;
    case RN_ANY: rc_emit(P, RO_ANY, nd->a, 0, 0, 0, 0); break;
    case RN_SET: rc_emit(P, RO_SET, nd->a, 0, 0, 0, 0); break;
    case RN_CAT: rc_chain(P, nd->kid, 0); break;
    case RN_ALT: {
        long ends[1024], *ep = ends, nend = 0, cap = 1024;
        int b = nd->kid;
        while (b >= 0) {
            int nb = RNN(P, b).next;
            if (nb >= 0) {
                sp = rc_emit(P, RO_SPLIT, 0, 0, 0, 0, 0);
                rc_node(P, b);
                if (nend >= cap) {
                    long *np = (long *)xmalloc(sizeof(long) * (size_t)cap * 2);
                    memcpy(np, ep, sizeof(long) * (size_t)nend);
                    if (ep != ends) free(ep);
                    ep = np; cap *= 2;
                }
                ep[nend++] = rc_emit(P, RO_JMP, 0, 0, 0, 0, 0);
                P->ins[sp].a = sp + 1;
                P->ins[sp].b = P->nins;
            } else {
                rc_node(P, b);
            }
            b = nb;
        }
        for (k = 0; k < nend; k++) P->ins[ep[k]].a = P->nins;
        if (ep != ends) free(ep);
        break; }
    case RN_GROUP:
        if (nd->a > 0) {
            long g = nd->a;
            rc_emit(P, RO_SAVE, 2 * g, 0, 0, 0, 0);
            rc_node(P, RNN(P, ni).kid);
            rc_emit(P, RO_SAVEEND, 2 * g + 1, g, 0, 0, 0);
        } else {
            rc_node(P, nd->kid);
        }
        break;
    case RN_AT: rc_emit(P, RO_AT, nd->a, 0, 0, 0, 0); break;
    case RN_BREF: rc_emit(P, RO_BREF, nd->a, nd->b, 0, 0, 0); break;
    case RN_FAIL: rc_emit(P, RO_FAIL, 0, 0, 0, 0, 0); break;
    case RN_LOOK: {
        long lo, hi;
        int behind = (int)nd->a, neg = (int)nd->b, body = nd->kid;
        rn_width(P, body, &lo, &hi);
        if (behind && lo != hi) {
            re_cleanup_run();
            re_throw_error("look-behind requires fixed-width pattern", NONE_V, 0, 0);
        }
        lk = rc_emit(P, RO_LOOK, neg ? 1 : 0, behind ? lo : 0, behind, 0, 0);
        rc_node(P, body);
        rc_emit(P, RO_LOOKEND, 0, 0, 0, 0, 0);
        P->ins[lk].d = P->nins;
        break; }
    case RN_ATOMIC:
        lk = rc_emit(P, RO_LOOK, 2, 0, 0, 0, 0);
        rc_node(P, nd->kid);
        rc_emit(P, RO_LOOKEND, 0, 0, 0, 0, 0);
        P->ins[lk].d = P->nins;
        break;
    case RN_COND: {
        long jmp;
        int yes = nd->kid, no = (int)nd->b;
        long g = nd->a;
        sp = rc_emit(P, RO_CONDREF, g, 0, 0, 0, 0);
        rc_node(P, yes);
        if (no >= 0) {
            jmp = rc_emit(P, RO_JMP, 0, 0, 0, 0, 0);
            P->ins[sp].b = P->nins;
            rc_node(P, no);
            P->ins[jmp].a = P->nins;
        } else {
            P->ins[sp].b = P->nins;
        }
        break; }
    case RN_REP: rc_rep(P, ni); break;
    default: break;
    }
}

/* first-byte analysis: returns 1 when the node can match the empty string */
static int rf_first(RParse *P, int ni, unsigned char *fs)
{
    RNode *nd = &RNN(P, ni);
    int k, nullable = 0, i;
    switch (nd->type) {
    case RN_EMPTY: case RN_AT: case RN_LOOK: return 1;
    case RN_FAIL: return 0;
    case RN_LIT:
        fs[nd->lb[0]] = 1;
        if (nd->b && nd->lb[0] < 128) { fs[to_low[nd->lb[0]]] = 1; fs[to_up[nd->lb[0]]] = 1; }
        return 0;
    case RN_ANY:
        for (i = 0; i < 256; i++) if (i >= 128 || i != '\n' || nd->a) fs[i] = 1;
        return 0;
    case RN_SET: {
        const RSet *st = &P->sets[nd->a];
        for (i = 0; i < 128; i++) if (BM_HAS(st->bm, i)) fs[i] = 1;
        if (!st->pure) for (i = 128; i < 256; i++) fs[i] = 1;
        return 0; }
    case RN_CAT:
        for (k = nd->kid; k >= 0; k = RNN(P, k).next)
            if (!rf_first(P, k, fs)) return 0;
        return 1;
    case RN_ALT:
        for (k = nd->kid; k >= 0; k = RNN(P, k).next)
            if (rf_first(P, k, fs)) nullable = 1;
        return nullable;
    case RN_GROUP: case RN_ATOMIC: return rf_first(P, nd->kid, fs);
    case RN_REP:
        nullable = rf_first(P, nd->kid, fs);
        return nullable || nd->a == 0 || nd->b == 0;
    case RN_COND:
        nullable = rf_first(P, nd->kid, fs);
        if (nd->b >= 0) { if (rf_first(P, (int)nd->b, fs)) nullable = 1; }
        else nullable = 1;
        return nullable;
    default:
        for (i = 0; i < 256; i++) fs[i] = 1;
        return 1;
    }
}
static int rf_anchored(RParse *P, int ni)
{
    RNode *nd = &RNN(P, ni);
    int k;
    switch (nd->type) {
    case RN_AT: return nd->a == RA_BOS;
    case RN_CAT: return rf_anchored(P, nd->kid);
    case RN_GROUP: case RN_ATOMIC: return rf_anchored(P, nd->kid);
    case RN_ALT:
        for (k = nd->kid; k >= 0; k = RNN(P, k).next) if (!rf_anchored(P, k)) return 0;
        return 1;
    default: return 0;
    }
}

/* build the program blob (a bytes value) */
static Value rc_build(RParse *P, int root, long flags)
{
    long hsz = (long)sizeof(RProg), isz = (long)sizeof(RI) * P->nins;
    long ssz = (long)sizeof(RSet) * P->nsets, rsz = (long)sizeof(long) * 2 * P->nrng;
    long total = hsz + isz + ssz + rsz + P->nlit;
    char *blob = (char *)xmalloc((size_t)total + 8);
    RProg *pg = (RProg *)blob;
    long i, cnt = 0, single = -1;
    unsigned char fs[256];
    Value v;
    memset(pg, 0, sizeof(RProg));
    pg->nins = P->nins; pg->nsets = P->nsets; pg->nrng = P->nrng; pg->litlen = P->nlit;
    pg->ngroups = P->ng;
    pg->li = 2 * (P->ng + 1);
    pg->ncaps = P->nslots;
    pg->flags = flags;
    pg->off_ins = hsz; pg->off_sets = hsz + isz; pg->off_rng = hsz + isz + ssz;
    pg->off_lit = hsz + isz + ssz + rsz;
    if (isz) memcpy(blob + pg->off_ins, P->ins, (size_t)isz);
    if (ssz) memcpy(blob + pg->off_sets, P->sets, (size_t)ssz);
    if (rsz) memcpy(blob + pg->off_rng, P->rng, (size_t)rsz);
    if (P->nlit) memcpy(blob + pg->off_lit, P->lit, (size_t)P->nlit);
    memset(fs, 0, sizeof(fs));
    pg->nullable = rf_first(P, root, fs);
    pg->anchored = rf_anchored(P, root);
    if (!pg->nullable) {
        pg->has_first = 1;
        memcpy(pg->first, fs, 256);
        for (i = 0; i < 256; i++) if (fs[i]) { cnt++; single = i; }
        pg->first_single = cnt == 1 ? single : -1;
    } else {
        pg->first_single = -1;
    }
    v = mkbytes(blob, total);
    free(blob);
    return v;
}

/* ---- virtual machine --------------------------------------------------- */
static int rstk_grow(void)
{
    long nc = rstk_cap ? rstk_cap * 2 : 4096;
    if (nc > RE_STK_MAX) {
        if (rstk_cap >= RE_STK_MAX) return 0;
        nc = RE_STK_MAX;
    }
    rstk = (RSE *)xrealloc(rstk, sizeof(RSE) * (size_t)nc);
    rstk_cap = nc;
    return 1;
}
static void rcaps_ensure(long n)
{
    if (n > rcaps_cap) {
        rcaps_cap = n < 64 ? 64 : n * 2;
        rcaps = (long *)xrealloc(rcaps, sizeof(long) * (size_t)rcaps_cap);
    }
}
/* one character of the REP1 operand at pos: byte length (> 0) or 0 */
static int re_r1(const RI *ip, const RSet *sets, const long *rng, const unsigned char *s, long pos,
                 long n)
{
    switch (ip->a) {
    case 0: return s[pos] == (unsigned char)ip->b ? 1 : 0;
    case 1: return s[pos] == '\n' ? 0 : (s[pos] < 128 ? 1 : re_u8len(s, pos, n));
    case 2: return s[pos] < 128 ? 1 : re_u8len(s, pos, n);
    default: return re_setmatch(&sets[ip->b], rng, s, pos, n);
    }
}
static int re_at(long kind, const unsigned char *s, long pos, long n)
{
    switch (kind) {
    case RA_BOS: return pos == 0;
    case RA_BOL: return pos == 0 || s[pos - 1] == '\n';
    case RA_EOL: return pos == n || (pos == n - 1 && s[pos] == '\n');
    case RA_EOLM: return pos == n || s[pos] == '\n';
    case RA_EOS: return pos == n;
    case RA_WB: return re_isword_before(s, pos, 0) != re_isword_after(s, pos, n, 0);
    case RA_NWB: return n != 0 && re_isword_before(s, pos, 0) == re_isword_after(s, pos, n, 0);
    case RA_WBA: return re_isword_before(s, pos, 1) != re_isword_after(s, pos, n, 1);
    default: return n != 0 && re_isword_before(s, pos, 1) == re_isword_after(s, pos, n, 1);
    }
}

#define RPUSH(K, A, B, C) do { \
    if (sp >= rstk_cap) { if (!rstk_grow()) return -2; st = rstk; } \
    st[sp].k = (K); st[sp].a = (A); st[sp].b = (B); st[sp].c = (C); sp++; } while (0)

/* run the program from `start`; returns the end position, -1 for no match, -2 for stack overflow.
   caps must be all -1 on entry; they are restored to -1 on failure. */
static long re_run(const RProg *pg, const unsigned char *s, long n, long start, long nostart,
                   long endreq)
{
    const RI *ins = RP_INS(pg);
    const RSet *sets = RP_SETS(pg);
    const long *rng = RP_RNG(pg);
    const unsigned char *lits = RP_LIT(pg);
    long *caps = rcaps;
    RSE *st = rstk;
    long sp = 0, pc = 0, pos = start, bar = -1, li = pg->li;
    const RI *ip;
    for (;;) {
      top:
        ip = ins + pc;
        switch (ip->op) {
        case RO_MATCH:
            if (endreq >= 0 && pos != endreq) break;
            if (pos == nostart) break;
            return pos;
        case RO_LIT: {
            long ln = ip->b;
            const unsigned char *lp = lits + ip->a;
            if (pos + ln <= n && s[pos] == lp[0]
                && (ln == 1 || memcmp(s + pos, lp, (size_t)ln) == 0)) {
                pos += ln; pc++; continue;
            }
            break; }
        case RO_LITI: {
            long ln = ip->b, k;
            const unsigned char *lp = lits + ip->a;
            if (pos + ln <= n) {
                for (k = 0; k < ln; k++) if (to_low[s[pos + k]] != lp[k]) break;
                if (k == ln) { pos += ln; pc++; continue; }
            }
            break; }
        case RO_ANY:
            if (pos < n && (ip->a || s[pos] != '\n')) {
                pos += s[pos] < 128 ? 1 : re_u8len(s, pos, n);
                pc++; continue;
            }
            break;
        case RO_SET:
            if (pos < n) {
                int l = re_setmatch(&sets[ip->a], rng, s, pos, n);
                if (l) { pos += l; pc++; continue; }
            }
            break;
        case RO_SPLIT:
            RPUSH(RK_ALT, ip->b, pos, 0);
            pc = ip->a;
            continue;
        case RO_JMP: pc = ip->a; continue;
        case RO_SAVE:
            RPUSH(RK_CAP, ip->a, caps[ip->a], 0);
            caps[ip->a] = pos;
            pc++; continue;
        case RO_SAVEEND:
            RPUSH(RK_CAP2, ip->a, caps[ip->a], caps[li]);
            caps[ip->a] = pos;
            caps[li] = ip->b;
            pc++; continue;
        case RO_AT:
            if (re_at(ip->a, s, pos, n)) { pc++; continue; }
            break;
        case RO_BREF: {
            long a0 = caps[2 * ip->a], b0 = caps[2 * ip->a + 1], ln, k;
            if (a0 < 0 || b0 < 0) break;
            ln = b0 - a0;
            if (pos + ln > n) break;
            if (ip->b) {
                for (k = 0; k < ln; k++) if (to_low[s[a0 + k]] != to_low[s[pos + k]]) break;
            } else {
                k = memcmp(s + a0, s + pos, (size_t)ln) == 0 ? ln : -1;
            }
            if (k == ln) { pos += ln; pc++; continue; }
            break; }
        case RO_CONDREF:
            if (caps[2 * ip->a] >= 0 && caps[2 * ip->a + 1] >= 0) pc++; else pc = ip->b;
            continue;
        case RO_FAIL: break;
        case RO_REP1: {
            long kind = ip->a, mn = ip->c, mx = ip->d, i = pos, cnt = 0, minpos = pos, l;
            int simple = kind == 0 || (kind == 3 && sets[ip->b].pure);
            if (ip->e == 1) {
                /* lazy: match the minimum, remember the rest */
                while (cnt < mn) {
                    if (i >= n || (l = re_r1(ip, sets, rng, s, i, n)) == 0) goto fail;
                    i += l; cnt++;
                }
                if (mx < 0 || mx > mn) RPUSH(RK_REPL, pc, i, mx < 0 ? -1 : mx - mn);
                pos = i; pc++; continue;
            }
            if (simple) {
                long bound = (mx < 0 || n - pos < mx) ? n : pos + mx;
                if (kind == 0) {
                    unsigned char c = (unsigned char)ip->b;
                    while (i < bound && s[i] == c) i++;
                } else {
                    const unsigned char *bm = sets[ip->b].bm;
                    while (i < bound && s[i] < 128 && BM_HAS(bm, s[i])) i++;
                }
                if (i - pos < mn) goto fail;
                minpos = pos + mn;
            } else if (mx < 0 && mn <= 1 && (kind == 1 || kind == 2)) {
                if (mn == 1) {
                    if (pos >= n || (l = re_r1(ip, sets, rng, s, pos, n)) == 0) goto fail;
                    i = pos + l; minpos = i;
                }
                if (kind == 2) i = n;
                else {
                    const void *q = i < n ? memchr(s + i, '\n', (size_t)(n - i)) : NULL;
                    i = q ? (long)((const unsigned char *)q - s) : n;
                }
            } else {
                if (mn == 0) minpos = pos;
                while ((mx < 0 || cnt < mx) && i < n && (l = re_r1(ip, sets, rng, s, i, n)) != 0) {
                    i += l; cnt++;
                    if (cnt == mn) minpos = i;
                }
                if (cnt < mn) goto fail;
            }
            if (ip->e == 0 && i > minpos) RPUSH(RK_REPG, pc, minpos, i);
            pos = i; pc++; continue; }
        case RO_CNTSET:
            RPUSH(RK_CAP, ip->a, caps[ip->a], 0);
            caps[ip->a] = 0;
            pc++; continue;
        case RO_LOOP: {
            long cnt = caps[ip->a];
            if (cnt < ip->b) { pc++; continue; }
            if (ip->c < 0 || cnt < ip->c) {
                if (ip->d) { RPUSH(RK_ALT, ip->e, pos, 0); pc++; continue; }
                RPUSH(RK_ALT, pc + 1, pos, 0);
                pc = ip->e; continue;
            }
            pc = ip->e; continue; }
        case RO_ENTER:
            RPUSH(RK_CAP, ip->a, caps[ip->a], 0);
            caps[ip->a]++;
            if (ip->b >= 0) {
                RPUSH(RK_CAP, ip->b, caps[ip->b], 0);
                caps[ip->b] = pos;
            }
            pc++; continue;
        case RO_ENDITER:
            if (ip->b >= 0 && pos == caps[ip->b] && caps[ip->a] >= ip->e) pc = ip->d;
            else pc = ip->c;
            continue;
        case RO_CHKEMPTY:
            if (pos == caps[ip->a]) pc = ip->b; else pc++;
            continue;
        case RO_LOOK: {
            RPUSH(RK_LOOK, pc, pos, bar);
            bar = sp - 1;
            if (ip->c) {
                long np = pos, w;
                for (w = 0; w < ip->b; w++) {
                    if (np <= 0) break;
                    np = re_back1(s, np);
                }
                if (w < ip->b) break;
                pos = np;
            }
            pc++; continue; }
        case RO_LOOKEND: {
            const RI *lk = ins + st[bar].a;
            long idx = bar, j, kept, spos = st[bar].b, pbar = st[bar].c;
            if (lk->c && pos != spos) break;
            if (lk->a == 1) {
                for (j = sp - 1; j > idx; j--) {
                    if (st[j].k == RK_CAP) caps[st[j].a] = st[j].b;
                    else if (st[j].k == RK_CAP2) { caps[st[j].a] = st[j].b; caps[li] = st[j].c; }
                }
                sp = idx; bar = pbar;
                break;
            }
            kept = idx;
            for (j = idx + 1; j < sp; j++)
                if (st[j].k == RK_CAP || st[j].k == RK_CAP2) st[kept++] = st[j];
            sp = kept; bar = pbar;
            if (lk->a == 0) pos = spos;
            pc = lk->d;
            continue; }
        default: break;
        }
      fail:
        for (;;) {
            RSE *e;
            if (sp == 0) return -1;
            e = &st[sp - 1];
            switch (e->k) {
            case RK_ALT:
                pc = e->a; pos = e->b; sp--;
                goto top;
            case RK_CAP: caps[e->a] = e->b; sp--; break;
            case RK_CAP2: caps[e->a] = e->b; caps[li] = e->c; sp--; break;
            case RK_REPG: {
                const RI *rp = ins + e->a;
                long cur = e->c, minp = e->b, c0 = -1;
                int simple = rp->a == 0 || (rp->a == 3 && sets[rp->b].pure);
                const RI *nx = rp + 1;
                if (nx->op == RO_LIT) c0 = lits[nx->a];
                for (;;) {
                    cur = simple ? cur - 1 : re_back1(s, cur);
                    if (cur < minp) cur = minp;
                    if (c0 < 0 || (cur < n && s[cur] == (unsigned char)c0)) break;
                    if (cur <= minp) { cur = -1; break; }
                }
                if (cur < 0) { sp--; break; }
                pos = cur; pc = e->a + 1;
                if (cur > minp) e->c = cur; else sp--;
                goto top; }
            case RK_REPL: {
                const RI *rp = ins + e->a;
                long cur = e->b;
                int l;
                if (cur < n && (l = re_r1(rp, sets, rng, s, cur, n)) != 0) {
                    pos = cur + l; pc = e->a + 1;
                    if (e->c == 1) sp--;
                    else { e->b = pos; if (e->c > 0) e->c--; }
                    goto top;
                }
                sp--;
                break; }
            case RK_LOOK: {
                const RI *lk = ins + e->a;
                long spos = e->b;
                bar = e->c;
                sp--;
                if (lk->a == 1) { pos = spos; pc = lk->d; goto top; }
                break; }
            default: sp--; break;
            }
        }
    }
}

/* match/search driver.  mode 0 search, 1 match, 2 fullmatch.  na: position where an empty
   match is not allowed.  Returns 1 on a match (caps in rcaps), 0 none; -2 stack overflow. */
static int re_exec(const RProg *pg, const unsigned char *s, long endpos, long pos, int mode,
    long na)
{
    long i, e, start;
    rcaps_ensure(pg->ncaps + 2);
    for (i = 0; i < pg->ncaps; i++) rcaps[i] = -1;
    if (!rstk) rstk_grow();
    if (pos > endpos) return 0;
    if (mode != 0) {
        e = re_run(pg, s, endpos, pos, pos == na ? pos : -1, mode == 2 ? endpos : -1);
        if (e == -2) return -2;
        if (e < 0) return 0;
        rcaps[0] = pos; rcaps[1] = e;
        return 1;
    }
    if (pg->anchored && pos > 0) return 0;
    start = pos;
    for (;;) {
        if (pg->has_first) {
            if (pg->first_single >= 0) {
                const void *q = start < endpos ? memchr(s + start, (int)pg->first_single,
                                                        (size_t)(endpos - start)) : NULL;
                if (!q) return 0;
                start = (long)((const unsigned char *)q - s);
            } else {
                while (start < endpos && !pg->first[s[start]]) start++;
                if (start >= endpos) return 0;
            }
        }
        e = re_run(pg, s, endpos, start, start == na ? start : -1, -1);
        if (e == -2) return -2;
        if (e >= 0) { rcaps[0] = start; rcaps[1] = e; return 1; }
        if (pg->anchored) return 0;
        if (start >= endpos) return 0;
        start += s[start] < 128 ? 1 : re_u8len(s, start, endpos);
    }
}

/* ---- pattern objects -------------------------------------------------- */
static const RProg *re_prog_of(Value pv)
{
    Value *p = amap_get(&AS_INST(pv)->a, RS_prog);
    return (const RProg *)AS_STR(*p)->s;
}
static int re_is_pattern(Value v)
{
    return v.t == T_INST && AS_INST(v)->cls == cls_re_pattern;
}
static int re_is_match(Value v)
{
    return v.t == T_INST && AS_INST(v)->cls == cls_re_match;
}
static Value re_attr(Value o, Str *nm)
{
    Value *p = amap_get(&AS_INST(o)->a, nm);
    return p ? *p : NONE_V;
}
static Str *re_str_arg(Value v)
{
    if (v.t == T_INST && AS_INST(v)->pay.t == T_STR) v = AS_INST(v)->pay;
    if (v.t != T_STR)
        throw_new(cls_TypeError, "expected string or bytes-like object, got '%.40s'", type_name(v));
    return AS_STR(v);
}
static void re_stack_overflow(void)
{
    throw_new(cls_RuntimeError, "regular expression too complex (backtracking stack limit)");
}
static void re_trim_stack(void)
{
    if (rstk_cap > 262144L) {
        free(rstk);
        rstk = NULL;
        rstk_cap = 0;
    }
}

static Value re_compile_new(Value patv, long flags)
{
    RParse P;
    Str *ps = AS_STR(patv);
    int root;
    long k;
    Value blob, dict, prog, iv;
    Inst *in;
    memset(&P, 0, sizeof(P));
    P.patv = patv;
    P.flags = P.gflags = flags;
    P.lbg = -1;
    re_cleanup = rp_cleanup_cb;
    re_cleanup_arg = &P;
    rt_init(&P.t, (const unsigned char *)ps->s, ps->len, patv);
    root = rp_parse_sub(&P, (flags & RF_X) != 0, 0);
    if (P.gflags & RF_L) re_throw_cls(cls_ValueError, "cannot use LOCALE flag with a str pattern");
    if (!(P.gflags & RF_A)) P.gflags |= RF_U;
    else if (P.gflags & RF_U)
        re_throw_cls(cls_ValueError, "ASCII and UNICODE flags are incompatible");
    if (P.t.nlen != 0) rp_fail(&P, "unbalanced parenthesis", 0);
    for (k = 0; k < P.ncr; k++) {
        if (P.cr_gid[k] > P.ng) {
            char buf[80];
            sprintf(buf, "invalid group reference %d", P.cr_gid[k]);
            re_pfail_at(patv, buf, P.cr_pos[k]);
        }
    }
    P.nslots = 2 * (P.ng + 1) + 1;
    rc_node(&P, root);
    rc_emit(&P, RO_MATCH, 0, 0, 0, 0, 0);
    blob = rc_build(&P, root, P.gflags);
#ifdef RE_DUMP
    { long q;
        for (q = 0; q < P.nins; q++)
            fprintf(stderr, "%3ld: op=%ld a=%ld b=%ld c=%ld d=%ld e=%ld\n", q, P.ins[q].op,
            P.ins[q].a, P.ins[q].b, P.ins[q].c, P.ins[q].d, P.ins[q].e);
    }
#endif
    VPUSH(blob);
    dict = mkobj(dict_new(), T_DICT);
    VPUSH(dict);
    for (k = 0; k < P.nnm; k++) {
        Value kv = mkstr((const char *)P.t.s + P.nm_off[k], P.nm_len[k]);
        VPUSH(kv);
        dict_set(AS_DICT(dict), kv, mkint(P.nm_gid[k]));
        vsp--;
    }
    prog = blob;
    k = P.ng;
    flags = P.gflags;
    re_cleanup = NULL;
    rp_free(&P);
    in = new_inst(cls_re_pattern);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    amap_set(&in->a, RS_pattern, patv);
    amap_set(&in->a, RS_flags, mkint(flags));
    amap_set(&in->a, RS_groups, mkint(k));
    amap_set(&in->a, RS_groupindex, dict);
    amap_set(&in->a, RS_prog, prog);
    vsp -= 3;
    return iv;
}

static Value re_get_pattern(Value patv, Value flagsv)
{
    long flags = 0;
    Value key, r;
    Buf kb;
    char tmp[24];
    if (flagsv.t != T_UNSET && flagsv.t != T_NONE) {
        if (flagsv.t != T_INT && flagsv.t != T_BOOL)
            throw_new(cls_TypeError, "flags must be an integer");
        flags = flagsv.u.i;
    }
    if (re_is_pattern(patv)) {
        if (flags)
            throw_new(cls_ValueError, "cannot process flags argument with a compiled pattern");
        return patv;
    }
    if (patv.t == T_INST && AS_INST(patv)->pay.t == T_STR) patv = AS_INST(patv)->pay;
    if (patv.t != T_STR)
        throw_new(cls_TypeError, "first argument must be string or compiled pattern");
    buf_init(&kb);
    sprintf(tmp, "%ld:", flags);
    buf_add(&kb, tmp);
    buf_addn(&kb, AS_STR(patv)->s, AS_STR(patv)->len);
    key = buf_value(&kb);
    VPUSH(key);
    if (dict_get(AS_DICT(re_cache_v), key, &r)) { vsp--; return r; }
    r = re_compile_new(patv, flags);
    VPUSH(r);
    if (AS_DICT(re_cache_v)->n > 500) dict_clear(AS_DICT(re_cache_v));
    dict_set(AS_DICT(re_cache_v), key, r);
    vsp -= 2;
    return r;
}

/* ---- match objects ---------------------------------------------------- */
static Value re_mk_match(Value patv, const RProg *pg, Value strv, long pos, long endpos)
{
    long ncap = 2 * (pg->ngroups + 1) + 1;
    Value blob, iv;
    Inst *in;
    blob = mkbytes((const char *)rcaps, ncap * (long)sizeof(long));
    VPUSH(blob);
    in = new_inst(cls_re_match);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    in->a.k = (Str **)xmalloc(sizeof(Str *) * 6);
    in->a.v = (Value *)xmalloc(sizeof(Value) * 6);
    in->a.cap = 6;
    amap_set(&in->a, RS_re, patv);
    amap_set(&in->a, RS_string, strv);
    amap_set(&in->a, RS_pos, mkint(pos));
    amap_set(&in->a, RS_endpos, mkint(endpos));
    amap_set(&in->a, RS_lastindex, rcaps[pg->li] > 0 ? mkint(rcaps[pg->li]) : NONE_V);
    amap_set(&in->a, RS_caps, blob);
    vsp -= 2;
    return iv;
}
static const long *re_m_caps(Value m, long *ng)
{
    Value *p;
    if (!re_is_match(m)) throw_new(cls_TypeError, "descriptor requires a 're.Match' object");
    p = amap_get(&AS_INST(m)->a, RS_caps);
    *ng = AS_STR(*p)->len / (long)sizeof(long) / 2 - 1 + 0;
    *ng = (AS_STR(*p)->len / (long)sizeof(long) - 1) / 2 - 1;
    return (const long *)AS_STR(*p)->s;
}
/* group index from an int or a name; -1 with IndexError raised */
static long re_m_index(Value m, Value g, long ng)
{
    if (g.t == T_INT || g.t == T_BOOL) {
        if (g.u.i < 0 || g.u.i > ng) throw_new(cls_IndexError, "no such group");
        return g.u.i;
    }
    if (g.t == T_STR) {
        Value gi = re_attr(re_attr(m, RS_re), RS_groupindex), r;
        if (gi.t == T_DICT && dict_get(AS_DICT(gi), g, &r)) return r.u.i;
    }
    throw_new(cls_IndexError, "no such group");
    return 0;
}
static Value re_m_group_val(Value m, const long *caps, long k)
{
    Str *s;
    if (caps[2 * k] < 0 || caps[2 * k + 1] < 0) return NONE_V;
    s = AS_STR(re_attr(m, RS_string));
    return mkstr(s->s + caps[2 * k], caps[2 * k + 1] - caps[2 * k]);
}
BI(mt_group)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    if (x->n <= 1) return re_m_group_val(A(0), caps, 0);
    if (x->n == 2) return re_m_group_val(A(0), caps, re_m_index(A(0), A(1), ng));
    {
        List *t = tuple_new(x->n - 1);
        int i;
        Value tv = mkobj(t, T_TUPLE);
        VPUSH(tv);
        for (i = 1; i < x->n; i++) t->a[i - 1] = NONE_V;
        for (i = 1; i < x->n; i++)
            t->a[i - 1] = re_m_group_val(A(0), caps, re_m_index(A(0), A(i), ng));
        vsp--;
        return tv;
    }
}
BI(mt_getitem)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    need(x, 2, 2, "__getitem__");
    return re_m_group_val(A(0), caps, re_m_index(A(0), A(1), ng));
}
BI(mt_groups)
{
    long ng, k;
    const long *caps = re_m_caps(A(0), &ng);
    Value dflt = x->n > 1 ? A(1) : kwget(x, "default");
    List *t = tuple_new(ng);
    Value tv = mkobj(t, T_TUPLE);
    if (dflt.t == T_UNSET) dflt = NONE_V;
    VPUSH(tv);
    for (k = 0; k < ng; k++) t->a[k] = NONE_V;
    for (k = 1; k <= ng; k++) {
        Value v = re_m_group_val(A(0), caps, k);
        t->a[k - 1] = v.t == T_NONE ? dflt : v;
    }
    vsp--;
    return tv;
}
BI(mt_groupdict)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    Value dflt = x->n > 1 ? A(1) : kwget(x, "default");
    Value gi = re_attr(re_attr(A(0), RS_re), RS_groupindex);
    Dict *d = dict_new(), *g = AS_DICT(gi);
    Value dv = mkobj(d, T_DICT);
    long i;
    if (dflt.t == T_UNSET) dflt = NONE_V;
    VPUSH(dv);
    for (i = 0; i < g->used; i++) {
        Value v;
        if (g->e[i].k.t == T_UNSET) continue;
        v = re_m_group_val(A(0), caps, g->e[i].v.u.i);
        dict_set(d, g->e[i].k, v.t == T_NONE ? dflt : v);
    }
    vsp--;
    return dv;
}
static long re_m_gidx(Args *x, long ng)
{
    if (x->n <= 1) return 0;
    return re_m_index(A(0), A(1), ng);
}
BI(mt_start)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    long k = re_m_gidx(x, ng);
    return mkint(caps[2 * k + 1] >= 0 && caps[2 * k] >= 0 ? caps[2 * k] : -1);
}
BI(mt_end)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    long k = re_m_gidx(x, ng);
    return mkint(caps[2 * k + 1] >= 0 && caps[2 * k] >= 0 ? caps[2 * k + 1] : -1);
}
BI(mt_span)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    long k = re_m_gidx(x, ng);
    if (caps[2 * k] < 0 || caps[2 * k + 1] < 0) return mktuple2(mkint(-1), mkint(-1));
    return mktuple2(mkint(caps[2 * k]), mkint(caps[2 * k + 1]));
}
BI(mt_regs)
{
    long ng, k;
    const long *caps = re_m_caps(A(0), &ng);
    List *t = tuple_new(ng + 1);
    Value tv = mkobj(t, T_TUPLE);
    VPUSH(tv);
    for (k = 0; k <= ng; k++) t->a[k] = NONE_V;
    for (k = 0; k <= ng; k++) {
        if (caps[2 * k] < 0 || caps[2 * k + 1] < 0) t->a[k] = mktuple2(mkint(-1), mkint(-1));
        else t->a[k] = mktuple2(mkint(caps[2 * k]), mkint(caps[2 * k + 1]));
    }
    vsp--;
    return tv;
}
BI(mt_lastgroup)
{
    Value li = re_attr(A(0), RS_lastindex), gi = re_attr(re_attr(A(0), RS_re), RS_groupindex);
    Dict *g;
    long i;
    if (li.t != T_INT || gi.t != T_DICT) return NONE_V;
    g = AS_DICT(gi);
    for (i = 0; i < g->used; i++)
        if (g->e[i].k.t != T_UNSET && g->e[i].v.u.i == li.u.i) return g->e[i].k;
    return NONE_V;
}
BI(mt_self)
{
    return A(0);
}
BI(mt_bool)
{
    NOKW(x);
    return TRUE_V;
}
BI(mt_repr)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    Buf b;
    char tmp[64];
    Value g0 = re_m_group_val(A(0), caps, 0), r;
    buf_init(&b);
    sprintf(tmp, "<re.Match object; span=(%ld, %ld), match=", caps[0], caps[1]);
    buf_add(&b, tmp);
    VPUSH(g0);
    r = py_repr(g0);
    buf_addn(&b, AS_STR(r)->s, AS_STR(r)->len);
    vsp--;
    buf_addc(&b, '>');
    return buf_value(&b);
}

/* ---- replacement templates ------------------------------------------- */
typedef struct RTpl {
    long *ops;      /* triples: kind (0 literal, 1 group), a, b */
    long nops, cap;
    Buf lit;
    int haslit;
} RTpl;
static void tpl_free(RTpl *T)
{
    free(T->ops);
    T->ops = NULL;
    if (T->haslit) buf_free(&T->lit);
    T->haslit = 0;
}
static void tpl_cleanup_cb(void *v)
{
    tpl_free((RTpl *)v);
}
static void tpl_add(RTpl *T, long kind, long a, long b)
{
    if (T->nops >= T->cap) {
        T->cap = T->cap ? T->cap * 2 : 16;
        T->ops = (long *)xrealloc(T->ops, sizeof(long) * 3 * (size_t)T->cap);
    }
    T->ops[3 * T->nops] = kind; T->ops[3 * T->nops + 1] = a; T->ops[3 * T->nops + 2] = b;
    T->nops++;
}
static void tpl_flush(RTpl *T, long *litstart)
{
    if (T->lit.n > *litstart) {
        tpl_add(T, 0, *litstart, T->lit.n - *litstart);
        *litstart = T->lit.n;
    }
}
static void tpl_cp(RTpl *T, long cp)
{
    unsigned char o[4];
    int l = re_u8enc(cp, o);
    buf_addn(&T->lit, (const char *)o, l);
}
static void tk_fail(RTok *t, Value patv, const char *msg, long offset)
{
    re_pfail_at(patv, msg, t->nstart - offset);
}
static void re_parse_template(Value replv, Value patv, RTpl *T)
{
    Str *rs = AS_STR(replv);
    RTok tk;
    long litstart = 0, st;
    int len;
    const RProg *pg = re_prog_of(patv);
    Value gi = re_attr(patv, RS_groupindex);
    memset(T, 0, sizeof(*T));
    buf_init(&T->lit);
    T->haslit = 1;
    re_cleanup = tpl_cleanup_cb;
    re_cleanup_arg = T;
    rt_init(&tk, (const unsigned char *)rs->s, rs->len, replv);
    while (rt_get(&tk, replv, &st, &len)) {
        int c;
        if (tk.s[st] != '\\') { buf_addn(&T->lit, (const char *)tk.s + st, len); continue; }
        c = len == 2 ? tk.s[st + 1] : 0;
        if (c == 'g') {
            long noff = tk.nstart, nl = 0, k;
            int alldig = 1;
            long idx = 0;
            if (!rt_match(&tk, replv, '<')) tk_fail(&tk, replv, "missing <", 0);
            noff = tk.nstart;
            for (;;) {
                long s2;
                int l2;
                if (!rt_get(&tk, replv, &s2, &l2)) {
                    if (nl == 0) tk_fail(&tk, replv, "missing group name", 0);
                    {
                        char buf[60];
                        sprintf(buf, "missing >, unterminated name");
                        tk_fail(&tk, replv, buf, nl);
                    }
                }
                if (l2 == 1 && tk.s[s2] == '>') {
                    if (nl == 0) tk_fail(&tk, replv, "missing group name", 1);
                    break;
                }
                nl += l2;
            }
            for (k = 0; k < nl; k++) if (!re_isdigitc(tk.s[noff + k])) alldig = 0;
            if (!alldig) {
                Value nm, r;
                if (!re_isident(tk.s + noff, nl)) {
                    Value rr = re_repr_range(tk.s, noff, nl);
                    char buf[400];
                    sprintf(buf, "bad character in group name %.300s", AS_STR(rr)->s);
                    tk_fail(&tk, replv, buf, nl + 1);
                }
                nm = mkstr((const char *)tk.s + noff, nl);
                if (gi.t != T_DICT || !dict_get(AS_DICT(gi), nm, &r)) {
                    Value rr = py_repr(nm);
                    char buf[400];
                    tpl_free(T);
                    re_cleanup = NULL;
                    sprintf(buf, "unknown group name %.300s", AS_STR(rr)->s);
                    throw_new(cls_IndexError, "%s", buf);
                }
                idx = r.u.i;
            } else {
                if (nl > 9) {
                    char buf[80];
                    sprintf(buf, "invalid group reference %s", "9999999999");
                    tk_fail(&tk, replv, buf, nl + 1);
                }
                for (k = 0; k < nl; k++) idx = idx * 10 + (tk.s[noff + k] - '0');
                if (idx > pg->ngroups) {
                    char buf[80];
                    sprintf(buf, "invalid group reference %ld", idx);
                    tk_fail(&tk, replv, buf, nl + 1);
                }
            }
            if (!alldig && idx > pg->ngroups) {
                char buf[80];
                sprintf(buf, "invalid group reference %ld", idx);
                tk_fail(&tk, replv, buf, nl + 1);
            }
            tpl_flush(T, &litstart);
            tpl_add(T, 1, idx, 0);
        } else if (c == '0') {
            long v = 0, got = 0;
            while (got < 2 && tk.nlen == 1 && re_isoctc(tk.s[tk.nstart])) {
                v = v * 8 + (tk.s[tk.nstart] - '0');
                got++;
                rt_advance(&tk, replv);
            }
            tpl_cp(T, v & 0xff);
        } else if (len == 2 && re_isdigitc(c)) {
            char esc[8];
            int el = 2, isoct = 0;
            long idx;
            esc[0] = '\\'; esc[1] = (char)c;
            if (tk.nlen == 1 && re_isdigitc(tk.s[tk.nstart])) {
                esc[el++] = (char)tk.s[tk.nstart];
                rt_advance(&tk, replv);
                if (re_isoctc(c) && re_isoctc(esc[2]) && tk.nlen == 1 && re_isoctc(tk.s[tk.nstart]))
                    {
                    long v;
                    esc[el++] = (char)tk.s[tk.nstart];
                    rt_advance(&tk, replv);
                    esc[el] = 0;
                    isoct = 1;
                    v = (esc[1] - '0') * 64 + (esc[2] - '0') * 8 + (esc[3] - '0');
                    if (v > 0377) {
                        char buf[80];
                        sprintf(buf, "octal escape value %s outside of range 0-0o377", esc);
                        tk_fail(&tk, replv, buf, el);
                    }
                    tpl_cp(T, v);
                }
            }
            if (!isoct) {
                esc[el] = 0;
                idx = atol(esc + 1);
                if (idx > pg->ngroups) {
                    char buf[80];
                    sprintf(buf, "invalid group reference %ld", idx);
                    tk_fail(&tk, replv, buf, el - 1);
                }
                tpl_flush(T, &litstart);
                tpl_add(T, 1, idx, 0);
            }
        } else {
            long cp;
            if (len == 2 && rp_simple_escape(c, &cp)) {
                tpl_cp(T, cp);
            } else {
                if (len == 2 && c < 128 && ch_alpha[c] && c != '_') {
                    char buf[60];
                    sprintf(buf, "bad escape \\%c", c);
                    tk_fail(&tk, replv, buf, len);
                }
                buf_addn(&T->lit, (const char *)tk.s + st, len);
            }
        }
    }
    tpl_flush(T, &litstart);
    re_cleanup = NULL;
}
static void tpl_eval(const RTpl *T, const long *caps, Str *s, Buf *out)
{
    long i;
    for (i = 0; i < T->nops; i++) {
        long k = T->ops[3 * i];
        if (k == 0) {
            buf_addn(out, T->lit.s + T->ops[3 * i + 1], T->ops[3 * i + 2]);
        } else {
            long g = T->ops[3 * i + 1];
            if (caps[2 * g] >= 0 && caps[2 * g + 1] >= 0)
                buf_addn(out, s->s + caps[2 * g], caps[2 * g + 1] - caps[2 * g]);
        }
    }
}
BI(mt_expand)
{
    long ng;
    const long *caps = re_m_caps(A(0), &ng);
    Value tv;
    RTpl T;
    Buf out;
    need(x, 2, 2, "expand");
    tv = A(1);
    if (!(tv.t == T_STR || (tv.t == T_INST && AS_INST(tv)->pay.t == T_STR)))
        throw_new(cls_TypeError, "expected str instance, %.40s found", type_name(tv));
    if (tv.t == T_INST) tv = AS_INST(tv)->pay;
    re_parse_template(tv, re_attr(A(0), RS_re), &T);
    buf_init(&out);
    tpl_eval(&T, caps, AS_STR(re_attr(A(0), RS_string)), &out);
    tpl_free(&T);
    return buf_value(&out);
}

/* ---- Pattern methods --------------------------------------------------- */
static void re_bounds(Str *s, Value posv, Value endv, long *pos, long *endpos)
{
    long p = 0, e = s->len;
    if (posv.t != T_UNSET && posv.t != T_NONE) p = to_ssize(posv);
    if (endv.t != T_UNSET && endv.t != T_NONE) e = to_ssize(endv);
    if (p < 0) p = 0; else if (p > s->len) p = s->len;
    if (e < 0) e = 0; else if (e > s->len) e = s->len;
    *pos = p; *endpos = e;
}
static Value re_argk(Args *x, int i, const char *kw)
{
    if (i < x->n) return x->a[i];
    return kwget(x, kw);
}
static Value re_do_match(Value patv, Value strv, Value posv, Value endv, int mode)
{
    const RProg *pg = re_prog_of(patv);
    Str *s = re_str_arg(strv);
    long pos, endpos;
    int r;
    re_bounds(s, posv, endv, &pos, &endpos);
    if (strv.t == T_INST) strv = AS_INST(strv)->pay;
    r = re_exec(pg, (const unsigned char *)s->s, endpos, pos, mode, -1);
    if (r == -2) re_stack_overflow();
    re_trim_stack();
    if (r <= 0) return NONE_V;
    return re_mk_match(patv, pg, strv, pos, endpos);
}
static Value pat_self(Args *x)
{
    if (x->n < 1 || !re_is_pattern(A(0)))
        throw_new(cls_TypeError, "descriptor requires a 're.Pattern' object");
    return A(0);
}
BI(pt_match)
{
    Value p = pat_self(x);
    return re_do_match(p, re_argk(x, 1, "string"), re_argk(x, 2, "pos"), re_argk(x, 3, "endpos"),
        1);
}
BI(pt_fullmatch)
{
    Value p = pat_self(x);
    return re_do_match(p, re_argk(x, 1, "string"), re_argk(x, 2, "pos"), re_argk(x, 3, "endpos"),
        2);
}
BI(pt_search)
{
    Value p = pat_self(x);
    return re_do_match(p, re_argk(x, 1, "string"), re_argk(x, 2, "pos"), re_argk(x, 3, "endpos"),
        0);
}

typedef struct RIt { long pos, endpos, na; int done; } RIt;
static int re_it_next(const RProg *pg, const unsigned char *s, RIt *it)
{
    int r;
    long e;
    if (it->done || it->pos > it->endpos) { it->done = 1; return 0; }
    r = re_exec(pg, s, it->endpos, it->pos, 0, it->na);
    if (r == -2) re_stack_overflow();
    if (r <= 0) { it->done = 1; return 0; }
    e = rcaps[1];
    it->na = (e == rcaps[0]) ? e : -1;
    it->pos = e;
    return 1;
}
static Value re_findall(Value patv, Value strv, Value posv, Value endv)
{
    const RProg *pg = re_prog_of(patv);
    Str *s = re_str_arg(strv);
    long ng = pg->ngroups, g;
    List *res = list_new(8);
    Value rv = mklist(res);
    RIt it;
    re_bounds(s, posv, endv, &it.pos, &it.endpos);
    it.na = -1; it.done = 0;
    VPUSH(rv);
    while (re_it_next(pg, (const unsigned char *)s->s, &it)) {
        if (ng == 0) {
            list_push(res, mkstr(s->s + rcaps[0], rcaps[1] - rcaps[0]));
        } else if (ng == 1) {
            if (rcaps[2] >= 0 && rcaps[3] >= 0)
                list_push(res, mkstr(s->s + rcaps[2], rcaps[3] - rcaps[2]));
            else list_push(res, mkstr("", 0));
        } else {
            List *t = tuple_new(ng);
            Value tv = mkobj(t, T_TUPLE);
            VPUSH(tv);
            for (g = 0; g < ng; g++) t->a[g] = NONE_V;
            for (g = 1; g <= ng; g++) {
                if (rcaps[2 * g] >= 0 && rcaps[2 * g + 1] >= 0)
                    t->a[g - 1] = mkstr(s->s + rcaps[2 * g], rcaps[2 * g + 1] - rcaps[2 * g]);
                else t->a[g - 1] = mkstr("", 0);
            }
            list_push(res, tv);
            vsp--;
        }
    }
    vsp--;
    re_trim_stack();
    return rv;
}
BI(pt_findall)
{
    Value p = pat_self(x);
    return re_findall(p, re_argk(x, 1, "string"), re_argk(x, 2, "pos"), re_argk(x, 3, "endpos"));
}
/* finditer: an iterator object holding the state in attributes */
static Value re_mk_iter(Class *c, Value patv, Value strv, Value posv, Value endv)
{
    Str *s = re_str_arg(strv);
    long pos, endpos;
    Inst *in;
    Value iv;
    re_bounds(s, posv, endv, &pos, &endpos);
    if (strv.t == T_INST) strv = AS_INST(strv)->pay;
    in = new_inst(c);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    amap_set(&in->a, RS_it_p, patv);
    amap_set(&in->a, RS_it_s, strv);
    amap_set(&in->a, RS_it_pos, mkint(pos));
    amap_set(&in->a, RS_it_end, mkint(endpos));
    amap_set(&in->a, RS_it_na, mkint(-1));
    vsp--;
    return iv;
}
BI(pt_finditer)
{
    Value p = pat_self(x);
    return re_mk_iter(cls_re_iter, p, re_argk(x, 1, "string"), re_argk(x, 2, "pos"),
                      re_argk(x, 3, "endpos"));
}
BI(pt_scanner)
{
    Value p = pat_self(x);
    return re_mk_iter(cls_re_scanner, p, re_argk(x, 1, "string"), re_argk(x, 2, "pos"),
                      re_argk(x, 3, "endpos"));
}
/* step an iterator object; mode 0 search, 1 match (scanner) */
static Value re_iter_step(Value self, int mode, int *ok)
{
    Inst *in = AS_INST(self);
    Value patv = re_attr(self, RS_it_p), strv = re_attr(self, RS_it_s);
    const RProg *pg = re_prog_of(patv);
    Str *s = AS_STR(strv);
    long pos = re_attr(self, RS_it_pos).u.i, endpos = re_attr(self, RS_it_end).u.i;
    long na = re_attr(self, RS_it_na).u.i, e;
    int r;
    *ok = 0;
    if (pos < 0 || pos > endpos) return NONE_V;
    r = re_exec(pg, (const unsigned char *)s->s, endpos, pos, mode, na);
    if (r == -2) re_stack_overflow();
    re_trim_stack();
    if (r <= 0) {
        amap_set(&in->a, RS_it_pos, mkint(-1));
        return NONE_V;
    }
    e = rcaps[1];
    amap_set(&in->a, RS_it_na, mkint(e == rcaps[0] ? e : -1));
    amap_set(&in->a, RS_it_pos, mkint(e));
    *ok = 1;
    return re_mk_match(patv, pg, strv, pos, endpos);
}
BI(it_next)
{
    int ok;
    Value m = re_iter_step(A(0), 0, &ok);
    if (!ok) throw_new(cls_StopIteration, "");
    return m;
}
BI(sc_search)
{
    int ok;
    return re_iter_step(A(0), 0, &ok);
}
BI(sc_match)
{
    int ok;
    return re_iter_step(A(0), 1, &ok);
}

static int re_callable(Value v)
{
    if (v.t == T_FUNC || v.t == T_BUILTIN || v.t == T_CLASS || v.t == T_BOUND) return 1;
    if (v.t == T_INST) return class_find(AS_INST(v)->cls, S_call) != NULL;
    return 0;
}
static Value re_subn(Value patv, Value replv, Value strv, Value countv, int want_n)
{
    const RProg *pg = re_prog_of(patv);
    Str *s = re_str_arg(strv);
    long count = 0, last = 0, cnt = 0, ms, me;
    int callable = re_callable(replv);
    RTpl T;
    RIt it;
    Buf out;
    Value r;
    if (countv.t != T_UNSET && countv.t != T_NONE) count = to_ssize(countv);
    if (strv.t == T_INST) strv = AS_INST(strv)->pay;
    memset(&T, 0, sizeof(T));
    if (!callable) {
        if (replv.t == T_INST && AS_INST(replv)->pay.t == T_STR) replv = AS_INST(replv)->pay;
        if (replv.t != T_STR)
            throw_new(cls_TypeError, "expected str instance, %.40s found", type_name(replv));
        if (memchr(AS_STR(replv)->s, '\\', (size_t)AS_STR(replv)->len)) {
            re_parse_template(replv, patv, &T);
        } else {
            T.lit.s = NULL;
        }
    }
    it.pos = 0; it.endpos = s->len; it.na = -1; it.done = 0;
    buf_init(&out);
    while (count == 0 || cnt < count) {
        if (!re_it_next(pg, (const unsigned char *)s->s, &it)) break;
        ms = rcaps[0]; me = rcaps[1];
        buf_addn(&out, s->s + last, ms - last);
        if (callable) {
            Value m = re_mk_match(patv, pg, strv, 0, s->len), res;
            VPUSH(m);
            res = call1(replv, m);
            vsp--;
            if (res.t == T_INST && AS_INST(res)->pay.t == T_STR) res = AS_INST(res)->pay;
            if (res.t != T_STR) {
                buf_free(&out);
                throw_new(cls_TypeError, "expected str instance, %.40s found", type_name(res));
            }
            buf_addn(&out, AS_STR(res)->s, AS_STR(res)->len);
        } else if (T.haslit) {
            tpl_eval(&T, rcaps, s, &out);
        } else {
            buf_addn(&out, AS_STR(replv)->s, AS_STR(replv)->len);
        }
        last = me;
        cnt++;
    }
    buf_addn(&out, s->s + last, s->len - last);
    if (T.haslit) tpl_free(&T);
    re_trim_stack();
    if (cnt == 0) { buf_free(&out); r = strv; }
    else r = buf_value(&out);
    if (want_n) {
        Value t;
        VPUSH(r);
        t = mktuple2(r, mkint(cnt));
        vsp--;
        return t;
    }
    return r;
}
BI(pt_sub)
{
    Value p = pat_self(x);
    return re_subn(p, re_argk(x, 1, "repl"), re_argk(x, 2, "string"), re_argk(x, 3, "count"), 0);
}
BI(pt_subn)
{
    Value p = pat_self(x);
    return re_subn(p, re_argk(x, 1, "repl"), re_argk(x, 2, "string"), re_argk(x, 3, "count"), 1);
}
static Value re_split(Value patv, Value strv, Value maxv)
{
    const RProg *pg = re_prog_of(patv);
    Str *s = re_str_arg(strv);
    long maxsplit = 0, last = 0, cnt = 0, g;
    List *res = list_new(8);
    Value rv = mklist(res);
    RIt it;
    if (maxv.t != T_UNSET && maxv.t != T_NONE) maxsplit = to_ssize(maxv);
    it.pos = 0; it.endpos = s->len; it.na = -1; it.done = 0;
    VPUSH(rv);
    while (maxsplit <= 0 || cnt < maxsplit) {
        if (!re_it_next(pg, (const unsigned char *)s->s, &it)) break;
        list_push(res, mkstr(s->s + last, rcaps[0] - last));
        for (g = 1; g <= pg->ngroups; g++) {
            if (rcaps[2 * g] >= 0 && rcaps[2 * g + 1] >= 0)
                list_push(res, mkstr(s->s + rcaps[2 * g], rcaps[2 * g + 1] - rcaps[2 * g]));
            else list_push(res, NONE_V);
        }
        last = rcaps[1];
        cnt++;
    }
    list_push(res, mkstr(s->s + last, s->len - last));
    vsp--;
    re_trim_stack();
    return rv;
}
BI(pt_split)
{
    Value p = pat_self(x);
    return re_split(p, re_argk(x, 1, "string"), re_argk(x, 2, "maxsplit"));
}
static const char *re_flagnames[] = { "re.IGNORECASE", "re.LOCALE", "re.MULTILINE", "re.DOTALL",
                                      "re.UNICODE", "re.VERBOSE", "re.DEBUG", "re.ASCII" };
static const long re_flagvals[] = { RF_I, RF_L, RF_M, RF_S, RF_U, RF_X, RF_D, RF_A };
BI(pt_repr)
{
    Value p = pat_self(x), pr = re_attr(p, RS_pattern), r;
    long flags = re_attr(p, RS_flags).u.i & ~(long)RF_U;
    Buf b;
    int i, first = 1;
    r = py_repr(pr);
    buf_init(&b);
    buf_add(&b, "re.compile(");
    if (AS_STR(r)->len > 200) buf_addn(&b, AS_STR(r)->s, 200);
    else buf_addn(&b, AS_STR(r)->s, AS_STR(r)->len);
    if (flags) {
        buf_add(&b, ", ");
        for (i = 0; i < 8; i++) {
            if (flags & re_flagvals[i]) {
                if (!first) buf_addc(&b, '|');
                buf_add(&b, re_flagnames[i]);
                first = 0;
                flags &= ~re_flagvals[i];
            }
        }
        if (flags) {
            char tmp[24];
            if (!first) buf_addc(&b, '|');
            sprintf(tmp, "0x%lx", flags);
            buf_add(&b, tmp);
        }
    }
    buf_addc(&b, ')');
    return buf_value(&b);
}
BI(pt_eq)
{
    Value a = A(0), b;
    need(x, 2, 2, "__eq__");
    b = A(1);
    if (!re_is_pattern(a) || !re_is_pattern(b)) return NotImpl_V;
    return mkbool(a.u.o == b.u.o ||
                  (re_attr(a, RS_flags).u.i == re_attr(b, RS_flags).u.i &&
                   py_eq(re_attr(a, RS_pattern), re_attr(b, RS_pattern))));
}
BI(pt_hash)
{
    Value a = A(0);
    return mkint((long)(py_hash(re_attr(a, RS_pattern)) ^ (unsigned long)re_attr(a, RS_flags).u.i) &
                 0x3fffffffL);
}
BI(pt_deepcopy)
{
    return A(0);
}

/* ---- module functions ------------------------------------------------- */
BI(re_compile)
{
    need(x, 1, 2, "compile");
    return re_get_pattern(A(0), re_argk(x, 1, "flags"));
}
BI(re_mmatch)
{
    Value p, f = re_argk(x, 2, "flags");
    need(x, 2, 3, "match");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_do_match(p, re_argk(x, 1, "string"), UNSET_V, UNSET_V, 1);
    vsp--;
    return p;
}
BI(re_mfullmatch)
{
    Value p, f = re_argk(x, 2, "flags");
    need(x, 2, 3, "fullmatch");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_do_match(p, re_argk(x, 1, "string"), UNSET_V, UNSET_V, 2);
    vsp--;
    return p;
}
BI(re_msearch)
{
    Value p, f = re_argk(x, 2, "flags");
    need(x, 2, 3, "search");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_do_match(p, re_argk(x, 1, "string"), UNSET_V, UNSET_V, 0);
    vsp--;
    return p;
}
BI(re_mfindall)
{
    Value p, f = re_argk(x, 2, "flags");
    need(x, 2, 3, "findall");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_findall(p, re_argk(x, 1, "string"), UNSET_V, UNSET_V);
    vsp--;
    return p;
}
BI(re_mfinditer)
{
    Value p, f = re_argk(x, 2, "flags");
    need(x, 2, 3, "finditer");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_mk_iter(cls_re_iter, p, re_argk(x, 1, "string"), UNSET_V, UNSET_V);
    vsp--;
    return p;
}
BI(re_msplit)
{
    Value p, f = re_argk(x, 3, "flags");
    need(x, 2, 4, "split");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_split(p, re_argk(x, 1, "string"), re_argk(x, 2, "maxsplit"));
    vsp--;
    return p;
}
BI(re_msub)
{
    Value p, f = re_argk(x, 4, "flags");
    need(x, 3, 5, "sub");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_subn(p, re_argk(x, 1, "repl"), re_argk(x, 2, "string"), re_argk(x, 3, "count"), 0);
    vsp--;
    return p;
}
BI(re_msubn)
{
    Value p, f = re_argk(x, 4, "flags");
    need(x, 3, 5, "subn");
    p = re_get_pattern(A(0), f);
    VPUSH(p);
    p = re_subn(p, re_argk(x, 1, "repl"), re_argk(x, 2, "string"), re_argk(x, 3, "count"), 1);
    vsp--;
    return p;
}
BI(re_purge)
{
    NOKW(x);
    dict_clear(AS_DICT(re_cache_v));
    return NONE_V;
}
BI(re_escape)
{
    Str *s;
    Buf b;
    long i;
    need(x, 1, 1, "escape");
    s = re_str_arg(A(0));
    buf_init(&b);
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        if (c != 0 && strchr("()[]{}?*+-|^$\\.&~# \t\n\r\v\f", c)) buf_addc(&b, '\\');
        buf_addc(&b, c);
    }
    return buf_value(&b);
}
BI(re_error_init)
{
    Value msg, pat, pos;
    Inst *in;
    if (x->n < 1 || A(0).t != T_INST)
        throw_new(cls_TypeError, "descriptor '__init__' requires an exception");
    msg = re_argk(x, 1, "msg");
    pat = re_argk(x, 2, "pattern");
    pos = re_argk(x, 3, "pos");
    if (msg.t == T_UNSET)
        throw_new(cls_TypeError, "error.__init__() missing 1 required positional argument: 'msg'");
    in = AS_INST(A(0));
    if (msg.t != T_STR) {
        msg = py_str(msg);
    }
    VPUSH(msg);
    if (pat.t != T_UNSET && pat.t != T_NONE && pos.t != T_UNSET && pos.t != T_NONE) {
        long p = to_ssize(pos);
        re_error_fill(in, msg, pat, p, 1);
    } else {
        re_error_fill(in, msg, pat.t == T_UNSET ? NONE_V : pat, 0, 0);
    }
    vsp--;
    return NONE_V;
}

static void re_init(void)
{
    Module *m = new_module("re");
    Class **b;
    int i;
    static const struct { const char *nm; long v; } fl[] = {
        { "I", RF_I }, { "IGNORECASE", RF_I }, { "L", RF_L }, { "LOCALE", RF_L },
        { "M", RF_M }, { "MULTILINE", RF_M }, { "S", RF_S }, { "DOTALL", RF_S },
        { "U", RF_U }, { "UNICODE", RF_U }, { "X", RF_X }, { "VERBOSE", RF_X },
        { "DEBUG", RF_D }, { "A", RF_A }, { "ASCII", RF_A }, { "NOFLAG", 0 }, { NULL, 0 }
    };
    RS_caps = intern_c("_caps"); RS_prog = intern_c("_prog"); RS_re = intern_c("re");
    RS_string = intern_c("string"); RS_pos = intern_c("pos"); RS_endpos = intern_c("endpos");
    RS_lastindex = intern_c("lastindex"); RS_pattern = intern_c("pattern");
    RS_flags = intern_c("flags"); RS_groups = intern_c("groups");
    RS_groupindex = intern_c("groupindex"); RS_msg = intern_c("msg");
    RS_lineno = intern_c("lineno"); RS_colno = intern_c("colno");
    RS_it_p = intern_c("_p"); RS_it_s = intern_c("_s"); RS_it_pos = intern_c("_pos");
    RS_it_end = intern_c("_end"); RS_it_na = intern_c("_na");
    re_cache_v = mkobj(dict_new(), T_DICT);
    mod_val(m, "_cache", re_cache_v);

    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_object;
    cls_re_pattern = make_class(intern_c("Pattern"), b, 1, intern_c("re"));
    def_bi(cls_re_pattern, "match", pt_match); def_bi(cls_re_pattern, "fullmatch", pt_fullmatch);
    def_bi(cls_re_pattern, "search", pt_search); def_bi(cls_re_pattern, "findall", pt_findall);
    def_bi(cls_re_pattern, "finditer", pt_finditer); def_bi(cls_re_pattern, "sub", pt_sub);
    def_bi(cls_re_pattern, "subn", pt_subn); def_bi(cls_re_pattern, "split", pt_split);
    def_bi(cls_re_pattern, "scanner", pt_scanner); def_bi(cls_re_pattern, "__repr__", pt_repr);
    def_bi(cls_re_pattern, "__eq__", pt_eq); def_bi(cls_re_pattern, "__hash__", pt_hash);
    def_bi(cls_re_pattern, "__copy__", mt_self);
    def_bi(cls_re_pattern, "__deepcopy__", pt_deepcopy);
    mod_val(m, "Pattern", mkobj(cls_re_pattern, T_CLASS));

    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_object;
    cls_re_match = make_class(intern_c("Match"), b, 1, intern_c("re"));
    def_bi(cls_re_match, "group", mt_group); def_bi(cls_re_match, "__getitem__", mt_getitem);
    def_bi(cls_re_match, "groups", mt_groups); def_bi(cls_re_match, "groupdict", mt_groupdict);
    def_bi(cls_re_match, "start", mt_start); def_bi(cls_re_match, "end", mt_end);
    def_bi(cls_re_match, "span", mt_span); def_bi(cls_re_match, "expand", mt_expand);
    def_bi(cls_re_match, "__repr__", mt_repr); def_bi(cls_re_match, "__bool__", mt_bool);
    def_bi(cls_re_match, "__copy__", mt_self); def_bi(cls_re_match, "__deepcopy__", pt_deepcopy);
    amap_set(&cls_re_match->ns, intern_c("lastgroup"),
        mk_prop(T_PROP, mkbuiltin("lastgroup", mt_lastgroup), UNSET_V));
    amap_set(&cls_re_match->ns, intern_c("regs"),
        mk_prop(T_PROP, mkbuiltin("regs", mt_regs), UNSET_V));
    cls_re_match->flags |= CF_PROPS;
    mod_val(m, "Match", mkobj(cls_re_match, T_CLASS));

    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_object;
    cls_re_iter = make_class(intern_c("callable_iterator"), b, 1, intern_c("builtins"));
    def_bi(cls_re_iter, "__next__", it_next); def_bi(cls_re_iter, "__iter__", mt_self);
    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_object;
    cls_re_scanner = make_class(intern_c("Scanner"), b, 1, intern_c("re"));
    def_bi(cls_re_scanner, "search", sc_search); def_bi(cls_re_scanner, "match", sc_match);

    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_Exception;
    cls_re_error = make_class(intern_c("error"), b, 1, intern_c("re"));
    def_bi(cls_re_error, "__init__", re_error_init);
    mod_val(m, "error", mkobj(cls_re_error, T_CLASS));
    mod_val(m, "PatternError", mkobj(cls_re_error, T_CLASS));

    for (i = 0; fl[i].nm; i++) mod_val(m, fl[i].nm, mkint(fl[i].v));
    mod_def(m, "compile", re_compile); mod_def(m, "match", re_mmatch);
    mod_def(m, "fullmatch", re_mfullmatch); mod_def(m, "search", re_msearch);
    mod_def(m, "findall", re_mfindall); mod_def(m, "finditer", re_mfinditer);
    mod_def(m, "split", re_msplit); mod_def(m, "sub", re_msub); mod_def(m, "subn", re_msubn);
    mod_def(m, "escape", re_escape); mod_def(m, "purge", re_purge);
}
