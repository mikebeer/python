/* =========================================================================
   p7_cms.c   optional native hook module _cms (compiled only with -DPY_CMS)

   _cms.diag58(subcode, data) -> bytes
       DIAGNOSE X'58', the 3270 virtual console interface of CP.  data is a 3270 command
       followed by its data stream (X'F5' Erase/Write + WCC + orders + EBCDIC text, or the
       single byte X'F6' Read Modified).  The result is the reply of a read (AID, cursor
       address, SBA + field data of every modified field) and empty for a write.
   _cms.host() -> "fake" | "cms" | "none"      which implementation answers diag58
   _cms.fake(on=True)                          select the in-memory 3270 host (tests)
   _cms.fake_push(aid, row, typed, crow, ccol) queue the answer to the next Read Modified:
                                               the user appended `typed` to the text of screen row
                                               `row`, pressed the key with AID byte `aid`, cursor
                                               at crow, ccol
   _cms.fake_screen() -> str                   the 24 rows of the fake screen, joined by newlines
   _cms.fake_cursor() -> (row, col)            cursor set by the last Erase/Write (IC order)

   The real call has to be written in assembler (or inline asm of the cross compiler) for the
   CMS host: see host_diag58 and PORTING.md section "Native hook _cms".  Everywhere else
   (no PY_HOST_CMS) diag58 raises NotImplementedError unless the fake host is selected.  The
   fake host also runs the environment variable PY_CMS_FAKE=1 (selected at the first call).
   ========================================================================= */
#define CMS_ROWS 24
#define CMS_COLS 80
#define CMS_CELLS 1920
#define CMS_QMAX 8
#define CMS_REPLY 2100

static int cms_fake_on = 0;
static int cms_env_done = 0;
static int cms_cur = 0;
static unsigned char cms_scr[CMS_CELLS];
static unsigned char cms_attr[CMS_CELLS];
static unsigned char cms_q[CMS_QMAX][CMS_REPLY];
static long cms_ql[CMS_QMAX];
static int cms_qn = 0;

/* 12-bit coded buffer addresses: the 64 character table */
static const unsigned char cms_addr[64] = {
    0x40, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F
};

/* ASCII 0x20..0x7E in code page 037 */
static const unsigned char cms_e037[95] = {
    0x40, 0x5A, 0x7F, 0x7B, 0x5B, 0x6C, 0x50, 0x7D, 0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0x7A, 0x5E, 0x4C, 0x7E, 0x6E, 0x6F,
    0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
    0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xBA, 0xE0, 0xBB, 0xB0, 0x6D,
    0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xC0, 0x4F, 0xD0, 0xA1
};

static int cms_from_addr(int b1, int b2)
{
    int i, j, hi = 0, lo = 0;
    for (i = 0; i < 64; i++) {
        if (cms_addr[i] == b1) hi = i;
        if (cms_addr[i] == b2) lo = i;
    }
    j = (hi << 6) | lo;
    return j % CMS_CELLS;
}

static int cms_to_ascii(int e)
{
    int i;
    for (i = 0; i < 95; i++)
        if (cms_e037[i] == e) return 0x20 + i;
    return 0x20;
}

/* The real host call.  Returns 0 and fills out/outl on success, 1 when DIAGNOSE X'58' is not
   available on this host (the default, also on CMS until the assembler stub is written). */
static int host_diag58(int sub, const unsigned char *in, long inl, unsigned char *out, long *outl)
{
    (void)sub; (void)in; (void)inl; (void)out;
    *outl = 0;
#ifdef PY_HOST_CMS
    /* CMS: load R1 = address of the data, R2 = length, R3 = sub code, issue DIAG 58, copy the
       reply to out and set *outl.  Not written yet, see PORTING.md. */
    return 1;
#else
    return 1;
#endif
}

/* ---- fake in-memory 3270 host ---------------------------------------- */
static void cms_fake_write(const unsigned char *d, long n)
{
    long i = 2;
    int pos = 0;
    memset(cms_scr, ' ', sizeof(cms_scr));
    memset(cms_attr, 0, sizeof(cms_attr));
    cms_cur = 0;
    while (i < n) {
        if (d[i] == 0x11 && i + 2 < n) {
            pos = cms_from_addr(d[i + 1], d[i + 2]);
            i += 3;
        } else if (d[i] == 0x13) {
            cms_cur = pos;
            i++;
        } else if (d[i] == 0x1D && i + 1 < n) {
            cms_attr[pos] = d[i + 1];
            cms_scr[pos] = ' ';
            pos = (pos + 1) % CMS_CELLS;
            i += 2;
        } else {
            cms_scr[pos] = (unsigned char)cms_to_ascii(d[i]);
            pos = (pos + 1) % CMS_CELLS;
            i++;
        }
    }
}

static int cms_row_len(int row)
{
    int n = CMS_COLS;
    while (n > 0 && cms_scr[row * CMS_COLS + n - 1] == ' ') n--;
    return n;
}

static int cms_ebcdic(int a)
{
    if (a >= 0x20 && a <= 0x7E) return cms_e037[a - 0x20];
    return 0x40;
}

static void cms_put_addr(unsigned char *r, long *n, int pos)
{
    r[(*n)++] = cms_addr[(pos >> 6) & 63];
    r[(*n)++] = cms_addr[pos & 63];
}

BI(cms_fake_push)
{
    unsigned char *r;
    const char *t;
    long n = 0, tl, rl;
    int aid, row, crow, ccol, i, slot;
    need(x, 5, 5, "fake_push");
    aid = (int)need_long(A(0), "aid");
    row = (int)need_long(A(1), "row");
    crow = (int)need_long(A(3), "crow");
    ccol = (int)need_long(A(4), "ccol");
    if (A(2).t != T_STR) throw_new(cls_TypeError, "typed must be str");
    t = AS_STR(A(2))->s;
    tl = (long)strlen(t);
    if (row < 1 || row > CMS_ROWS) throw_new(cls_ValueError, "row out of range");
    if (cms_qn >= CMS_QMAX) throw_new(cls_RuntimeError, "fake 3270 host: reply queue full");
    slot = cms_qn++;
    r = cms_q[slot];
    r[n++] = (unsigned char)aid;
    cms_put_addr(r, &n, ((crow - 1) * CMS_COLS + (ccol - 1) + 1) % CMS_CELLS);
    if (tl > 0) {
        rl = cms_row_len(row - 1);
        r[n++] = 0x11;
        cms_put_addr(r, &n, ((row - 1) * CMS_COLS + 1) % CMS_CELLS);
        for (i = 1; i < rl; i++)
            r[n++] = (unsigned char)cms_ebcdic(cms_scr[(row - 1) * CMS_COLS + i]);
        for (i = 0; i < tl && n < CMS_REPLY - 1; i++)
            r[n++] = (unsigned char)cms_ebcdic((unsigned char)t[i]);
    }
    cms_ql[slot] = n;
    return NONE_V;
}

BI(cms_fake)
{
    need(x, 0, 1, "fake");
    cms_fake_on = x->n > 0 ? (need_long(A(0), "on") != 0) : 1;
    cms_env_done = 1;
    if (cms_fake_on) {
        memset(cms_scr, ' ', sizeof(cms_scr));
        memset(cms_attr, 0, sizeof(cms_attr));
        cms_cur = 0;
        cms_qn = 0;
    }
    return NONE_V;
}

static void cms_select(void)
{
    const char *e;
    if (!cms_env_done) {
        cms_env_done = 1;
        e = host_getenv("PY_CMS_FAKE");
        if (e != NULL && e[0] == '1') cms_fake_on = 1;
    }
}

BI(cms_host)
{
    need(x, 0, 0, "host");
    cms_select();
    if (cms_fake_on) return mkstr("fake", 4);
#ifdef PY_HOST_CMS
    return mkstr("cms", 3);
#else
    return mkstr("none", 4);
#endif
}

BI(cms_diag58)
{
    const char *p;
    long n, rn = 0, i;
    unsigned char out[CMS_REPLY];
    int sub, k;
    need(x, 2, 2, "diag58");
    sub = (int)need_long(A(0), "subcode");
    if (!bytes_get(A(1), &p, &n)) throw_new(cls_TypeError, "data must be bytes");
    cms_select();
    if (!cms_fake_on) {
        k = host_diag58(sub, (const unsigned char *)p, n, out, &rn);
        if (k != 0)
            throw_new(cls_NotImplementedError,
                      "DIAGNOSE X'58' is not available on this host (needs CMS; _cms.fake() selects"
                      " the in-memory 3270 host)");
        return mkbytes((const char *)out, rn);
    }
    if (n < 1) throw_new(cls_ValueError, "empty 3270 data stream");
    if ((unsigned char)p[0] == 0xF5) {
        cms_fake_write((const unsigned char *)p, n);
        return mkbytes("", 0);
    }
    if ((unsigned char)p[0] == 0xF6) {
        if (cms_qn > 0) {
            rn = cms_ql[0];
            memcpy(out, cms_q[0], (size_t)rn);
            for (i = 1; i < cms_qn; i++) {
                memcpy(cms_q[i - 1], cms_q[i], (size_t)cms_ql[i]);
                cms_ql[i - 1] = cms_ql[i];
            }
            cms_qn--;
        } else {
            rn = 0;
            out[rn++] = 0x7D;
            cms_put_addr(out, &rn, cms_cur);
        }
        return mkbytes((const char *)out, rn);
    }
    throw_new(cls_ValueError, "unknown 3270 command X'%02X'", (unsigned)(unsigned char)p[0]);
    return NONE_V;
}

BI(cms_fake_screen)
{
    char buf[CMS_CELLS + CMS_ROWS + 1];
    long n = 0;
    int r, c;
    need(x, 0, 0, "fake_screen");
    for (r = 0; r < CMS_ROWS; r++) {
        for (c = 0; c < CMS_COLS; c++) buf[n++] = (char)cms_scr[r * CMS_COLS + c];
        if (r < CMS_ROWS - 1) buf[n++] = '\n';
    }
    return mkstr(buf, n);
}

BI(cms_fake_cursor)
{
    Value o[2];
    need(x, 0, 0, "fake_cursor");
    o[0] = mkint(cms_cur / CMS_COLS + 1);
    o[1] = mkint(cms_cur % CMS_COLS + 1);
    return mktuple_from(o, 2);
}

static void cms_init(void)
{
    Module *m = new_module("_cms");
    memset(cms_scr, ' ', sizeof(cms_scr));
    memset(cms_attr, 0, sizeof(cms_attr));
    mod_def(m, "diag58", cms_diag58);
    mod_def(m, "host", cms_host);
    mod_def(m, "fake", cms_fake);
    mod_def(m, "fake_push", cms_fake_push);
    mod_def(m, "fake_screen", cms_fake_screen);
    mod_def(m, "fake_cursor", cms_fake_cursor);
}
