/* rxdf.c - see rxdf.h. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "rxdf.h"

static char derr[200];
const char *rxdf_error(void) { return derr; }
static void *dfail(const char *m) { strncpy(derr, m, sizeof derr - 1); return NULL; }
static double nanv(void) { volatile double z = 0.0; return z / z; }
static char *sdup(const char *s) { size_t n = strlen(s); char *p = (char *)malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

rxdf *rxdf_new(void) { rxdf *d = (rxdf *)calloc(1, sizeof *d); if (d) d->refs = 1; return d; }
rxdf *rxdf_retain(rxdf *d) { if (d) d->refs++; return d; }
void rxdf_release(rxdf *d)
{
    long c, r;
    if (!d || --d->refs > 0) return;
    for (c = 0; c < d->ncols; c++) {
        free(d->names[c]);
        if (d->isstr[c]) { for (r = 0; r < d->nrows; r++) free(d->str[c][r]); free(d->str[c]); }
        else free(d->num[c]);
    }
    free(d->names); free(d->isstr); free(d->isint); free(d->num); free(d->str); free(d);
}
long rxdf_find(const rxdf *d, const char *name)
{
    long c;
    for (c = 0; c < d->ncols; c++) if (!strcmp(d->names[c], name)) return c;
    return -1;
}
/* append an empty column slot */
static long grow(rxdf *d, const char *name, int isstr)
{
    long n = d->ncols + 1;
    char **nm = (char **)realloc(d->names, n * sizeof *nm);
    int *is = (int *)realloc(d->isstr, n * sizeof *is), *ii = (int *)realloc(d->isint, n * sizeof *ii);
    double **nu = (double **)realloc(d->num, n * sizeof *nu);
    char ***st = (char ***)realloc(d->str, n * sizeof *st);
    if (!nm || !is || !ii || !nu || !st) return -1;
    d->names = nm; d->isstr = is; d->isint = ii; d->num = nu; d->str = st;
    d->names[n - 1] = sdup(name); d->isstr[n - 1] = isstr; d->isint[n - 1] = 0; d->num[n - 1] = NULL; d->str[n - 1] = NULL;
    d->ncols = n;
    return n - 1;
}
static int add_num_raw(rxdf *d, const char *name, double *vals, long n, int isint)
{
    long c;
    if (d->ncols == 0) d->nrows = n;
    else if (n != d->nrows) { free(vals); dfail("column length does not match the table"); return 0; }
    if (rxdf_find(d, name) >= 0) { free(vals); dfail("duplicate column name"); return 0; }
    c = grow(d, name, 0);
    if (c < 0) { free(vals); dfail("out of memory"); return 0; }
    d->num[c] = vals; d->isint[c] = isint;
    return 1;
}
static int add_str_raw(rxdf *d, const char *name, char **vals, long n)
{
    long c, r;
    if (d->ncols == 0) d->nrows = n;
    else if (n != d->nrows || rxdf_find(d, name) >= 0) {
        for (r = 0; r < n; r++) free(vals[r]);
        free(vals); dfail("column length mismatch or duplicate column name"); return 0;
    }
    c = grow(d, name, 1);
    if (c < 0) { dfail("out of memory"); return 0; }
    d->str[c] = vals;
    return 1;
}
int rxdf_add_num(rxdf *d, const char *name, const rxnp *a)
{
    long k, n = a->size; double *v; int isint = a->dt == RXNP_I64;
    if (a->ndim != 1) { dfail("column must be a 1-d array"); return 0; }
    v = (double *)malloc((n ? n : 1) * sizeof *v);
    if (!v) { dfail("out of memory"); return 0; }
    for (k = 0; k < n; k++) v[k] = isint ? (double)a->i[k] : a->f[k];
    return add_num_raw(d, name, v, n, isint);
}
int rxdf_add_str(rxdf *d, const char *name, const char *csv)
{
    long n = 0, cap = 8; char **v = (char **)malloc(cap * sizeof *v); const char *p = csv;
    if (!v) { dfail("out of memory"); return 0; }
    for (;;) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (n == cap) { cap *= 2; v = (char **)realloc(v, cap * sizeof *v); if (!v) { dfail("out of memory"); return 0; } }
        v[n] = (char *)malloc(len + 1); memcpy(v[n], p, len); v[n][len] = 0; n++;
        if (!e) break;
        p = e + 1;
    }
    return add_str_raw(d, name, v, n);
}

/* ------------------------------------------------------------------ number formatting */
static void fmt_double(char *buf, double v, int asint)
{
    int prec;
    if (v != v) { buf[0] = 0; return; }                     /* NaN -> empty (pandas to_csv) */
    if (v > 1e308 || v < -1e308) { strcpy(buf, v > 0 ? "inf" : "-inf"); return; }
    if (asint) { sprintf(buf, "%.0f", v); return; }
    for (prec = 15; prec <= 17; prec++) { sprintf(buf, "%.*g", prec, v); if (strtod(buf, NULL) == v) break; }
    if (!strpbrk(buf, ".eEn")) strcat(buf, ".0");
}

/* ------------------------------------------------------------------ CSV */
typedef struct { char *s; size_t n, cap; } sb;
static void sb_add(sb *b, const char *t, size_t l)
{
    if (b->n + l + 1 > b->cap) { b->cap = (b->cap ? b->cap * 2 : 256); while (b->cap < b->n + l + 1) b->cap *= 2; b->s = (char *)realloc(b->s, b->cap); }
    memcpy(b->s + b->n, t, l); b->n += l; b->s[b->n] = 0;
}
/* split one record starting at *pp; fields malloc'd; returns number of fields (0 at end of text) */
static long csv_record(const char **pp, char ***out)
{
    const char *p = *pp; long n = 0, cap = 8; char **f;
    if (!*p) return 0;
    f = (char **)malloc(cap * sizeof *f);
    for (;;) {
        sb b; b.s = NULL; b.n = b.cap = 0; sb_add(&b, "", 0);
        if (*p == '"') {
            p++;
            for (;;) {
                if (*p == '"' && p[1] == '"') { sb_add(&b, "\"", 1); p += 2; }
                else if (*p == '"') { p++; break; }
                else if (!*p) break;
                else { sb_add(&b, p, 1); p++; }
            }
        } else while (*p && *p != ',' && *p != '\n' && *p != '\r') { sb_add(&b, p, 1); p++; }
        if (n == cap) { cap *= 2; f = (char **)realloc(f, cap * sizeof *f); }
        f[n++] = b.s;
        if (*p == ',') { p++; continue; }
        if (*p == '\r') p++;
        if (*p == '\n') p++;
        break;
    }
    *pp = p; *out = f;
    return n;
}
static int parse_num(const char *s, double *v, int *isintsyntax)
{
    char *e; const char *q = s;
    while (isspace((unsigned char)*q)) q++;
    if (!*q) return 2;                                       /* empty: NaN */
    *v = strtod(q, &e);
    if (e == q) return 0;
    while (isspace((unsigned char)*e)) e++;
    if (*e) return 0;
    if (strpbrk(q, ".eEnN")) *isintsyntax = 0;
    return 1;
}
rxdf *rxdf_parse_csv(const char *text)
{
    const char *p = text; char **hdr, **rec; long nh, nf, nr = 0, cap = 16, r, c; char ***cells; rxdf *d;
    nh = csv_record(&p, &hdr);
    if (nh == 0) return (rxdf *)dfail("empty csv");
    cells = (char ***)malloc(cap * sizeof *cells);
    while ((nf = csv_record(&p, &rec)) > 0) {
        if (nf == 1 && !rec[0][0]) { free(rec[0]); free(rec); continue; }   /* blank line */
        if (nf != nh) { return (rxdf *)dfail("csv row has a different number of fields than the header"); }
        if (nr == cap) { cap *= 2; cells = (char ***)realloc(cells, cap * sizeof *cells); }
        cells[nr++] = rec;
    }
    d = rxdf_new();
    for (c = 0; c < nh; c++) {
        int numeric = 1, isint = 1, anyval = 0;
        for (r = 0; r < nr && numeric; r++) {
            double v; int k = parse_num(cells[r][c], &v, &isint);
            if (k == 0) numeric = 0; else if (k == 2) isint = 0; else anyval = 1;
        }
        if (nr == 0) numeric = 1;
        if (numeric && anyval + (nr == 0) > 0) {
            double *v = (double *)malloc((nr ? nr : 1) * sizeof *v);
            for (r = 0; r < nr; r++) { int dummy = 1; if (parse_num(cells[r][c], &v[r], &dummy) == 2) v[r] = nanv(); }
            add_num_raw(d, hdr[c], v, nr, isint);
        } else {
            char **v = (char **)malloc((nr ? nr : 1) * sizeof *v);
            for (r = 0; r < nr; r++) v[r] = sdup(cells[r][c]);
            add_str_raw(d, hdr[c], v, nr);
        }
    }
    for (r = 0; r < nr; r++) { for (c = 0; c < nh; c++) free(cells[r][c]); free(cells[r]); }
    free(cells);
    for (c = 0; c < nh; c++) free(hdr[c]);
    free(hdr);
    d->nrows = nr;
    return d;
}
rxdf *rxdf_read_csv(const char *path)
{
    FILE *f = fopen(path, "rb"); long n; char *t; rxdf *d;
    if (!f) return (rxdf *)dfail("cannot open file");
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    t = (char *)malloc(n + 1);
    if (!t || fread(t, 1, n, f) != (size_t)n) { fclose(f); free(t); return (rxdf *)dfail("cannot read file"); }
    t[n] = 0; fclose(f);
    d = rxdf_parse_csv(t); free(t);
    return d;
}
static void csv_field(FILE *f, const char *s)
{
    if (strpbrk(s, ",\"\n\r")) {
        fputc('"', f);
        for (; *s; s++) { if (*s == '"') fputc('"', f); fputc(*s, f); }
        fputc('"', f);
    } else fputs(s, f);
}
int rxdf_to_csv(const rxdf *d, const char *path)
{
    FILE *f = fopen(path, "wb"); long r, c; char buf[64];
    if (!f) { dfail("cannot open file for writing"); return 0; }
    for (c = 0; c < d->ncols; c++) { if (c) fputc(',', f); csv_field(f, d->names[c]); }
    fputc('\n', f);
    for (r = 0; r < d->nrows; r++) {
        for (c = 0; c < d->ncols; c++) {
            if (c) fputc(',', f);
            if (d->isstr[c]) csv_field(f, d->str[c][r]);
            else { fmt_double(buf, d->num[c][r], d->isint[c]); fputs(buf, f); }
        }
        fputc('\n', f);
    }
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------ print */
char *rxdf_to_text(const rxdf *d, long maxrows)
{
    long r, c, nr = d->nrows < maxrows ? d->nrows : maxrows; size_t *w; char buf[64]; sb o; const char *cell;
    w = (size_t *)calloc(d->ncols + 1, sizeof *w);
    o.s = NULL; o.n = o.cap = 0; sb_add(&o, "", 0);
    for (c = 0; c < d->ncols; c++) {
        w[c] = strlen(d->names[c]);
        for (r = 0; r < nr; r++) {
            size_t l;
            if (d->isstr[c]) l = strlen(d->str[c][r]);
            else { fmt_double(buf, d->num[c][r], d->isint[c]); l = d->num[c][r] != d->num[c][r] ? 3 : strlen(buf); }
            if (l > w[c]) w[c] = l;
        }
    }
    for (r = -1; r < nr; r++) {
        char idx[24]; sprintf(idx, "%ld", r);
        if (r < 0) sprintf(idx, "%*s", 3, ""); else sprintf(idx, "%-3ld", r);
        sb_add(&o, idx, strlen(idx));
        for (c = 0; c < d->ncols; c++) {
            char tmp[400]; size_t pad;
            if (r < 0) cell = d->names[c];
            else if (d->isstr[c]) cell = d->str[c][r];
            else if (d->num[c][r] != d->num[c][r]) cell = "NaN";
            else { fmt_double(buf, d->num[c][r], d->isint[c]); cell = buf; }
            pad = w[c] > strlen(cell) ? w[c] - strlen(cell) : 0;
            sprintf(tmp, "  %*s%s", (int)pad, "", cell);
            sb_add(&o, tmp, strlen(tmp));
        }
        sb_add(&o, "\n", 1);
    }
    if (nr < d->nrows) { char tmp[64]; sprintf(tmp, "[%ld rows x %ld columns]\n", d->nrows, d->ncols); sb_add(&o, tmp, strlen(tmp)); }
    free(w);
    return o.s;
}

/* ------------------------------------------------------------------ column / row ops */
rxnp *rxdf_col(const rxdf *d, const char *name)
{
    long c = rxdf_find(d, name), sh[1], k; rxnp *a;
    if (c < 0) return (rxnp *)dfail("no such column");
    if (d->isstr[c]) return (rxnp *)dfail("column is not numeric");
    sh[0] = d->nrows; a = rxnp_new(d->isint[c] ? RXNP_I64 : RXNP_F64, 1, sh);
    if (!a) return NULL;
    for (k = 0; k < d->nrows; k++) { if (a->dt == RXNP_I64) a->i[k] = (long)d->num[c][k]; else a->f[k] = d->num[c][k]; }
    return a;
}
/* copy rows listed in idx (n entries, -1 = missing row -> NaN/empty) from d into a new table */
static rxdf *take(const rxdf *d, const long *idx, long n)
{
    rxdf *o = rxdf_new(); long c, r;
    for (c = 0; c < d->ncols; c++) {
        if (d->isstr[c]) {
            char **v = (char **)malloc((n ? n : 1) * sizeof *v);
            for (r = 0; r < n; r++) v[r] = sdup(idx[r] < 0 ? "" : d->str[c][idx[r]]);
            add_str_raw(o, d->names[c], v, n);
        } else {
            double *v = (double *)malloc((n ? n : 1) * sizeof *v); int anymiss = 0;
            for (r = 0; r < n; r++) { v[r] = idx[r] < 0 ? nanv() : d->num[c][idx[r]]; if (idx[r] < 0) anymiss = 1; }
            add_num_raw(o, d->names[c], v, n, d->isint[c] && !anymiss);
        }
    }
    o->nrows = n;
    return o;
}
rxdf *rxdf_head(const rxdf *d, long n)
{
    long *idx, r; rxdf *o;
    if (n > d->nrows) n = d->nrows;
    if (n < 0) n = 0;
    idx = (long *)malloc((n ? n : 1) * sizeof *idx);
    for (r = 0; r < n; r++) idx[r] = r;
    o = take(d, idx, n); free(idx);
    return o;
}
rxdf *rxdf_filter(const rxdf *d, const char *col, const char *op, const char *value)
{
    long c = rxdf_find(d, col), r, n = 0, *idx; double v = 0; rxdf *o; int code;
    if (c < 0) return (rxdf *)dfail("no such column");
    if (!strcmp(op, "<")) code = 0; else if (!strcmp(op, "<=")) code = 1; else if (!strcmp(op, ">")) code = 2;
    else if (!strcmp(op, ">=")) code = 3; else if (!strcmp(op, "==")) code = 4; else if (!strcmp(op, "!=")) code = 5;
    else return (rxdf *)dfail("operator must be one of < <= > >= == !=");
    if (!d->isstr[c]) { char *e; v = strtod(value, &e); if (e == value || *e) return (rxdf *)dfail("value is not a number"); }
    else if (code != 4 && code != 5) return (rxdf *)dfail("string columns support only == and !=");
    idx = (long *)malloc((d->nrows ? d->nrows : 1) * sizeof *idx);
    for (r = 0; r < d->nrows; r++) {
        int keep;
        if (d->isstr[c]) { int eq = !strcmp(d->str[c][r], value); keep = code == 4 ? eq : !eq; }
        else {
            double x = d->num[c][r];
            switch (code) { case 0: keep = x < v; break; case 1: keep = x <= v; break; case 2: keep = x > v; break;
                            case 3: keep = x >= v; break; case 4: keep = x == v; break; default: keep = x != v; break; }
        }
        if (keep) idx[n++] = r;
    }
    o = take(d, idx, n); free(idx);
    return o;
}
static const rxdf *g_sortd; static long g_sortc; static int g_asc;
static int cmp_rows(const void *pa, const void *pb)
{
    long a = *(const long *)pa, b = *(const long *)pb; int r;
    if (g_sortd->isstr[g_sortc]) r = strcmp(g_sortd->str[g_sortc][a], g_sortd->str[g_sortc][b]);
    else {
        double x = g_sortd->num[g_sortc][a], y = g_sortd->num[g_sortc][b];
        if (x != x || y != y) { r = (x != x) - (y != y); return r ? r : (a > b) - (a < b); }   /* NaN last, both orders */
        r = (x > y) - (x < y);
    }
    if (r == 0) return (a > b) - (a < b);                  /* stable */
    return g_asc ? r : -r;
}
rxdf *rxdf_sort(const rxdf *d, const char *col, int ascending)
{
    long c = rxdf_find(d, col), r, *idx; rxdf *o;
    if (c < 0) return (rxdf *)dfail("no such column");
    idx = (long *)malloc((d->nrows ? d->nrows : 1) * sizeof *idx);
    for (r = 0; r < d->nrows; r++) idx[r] = r;
    g_sortd = d; g_sortc = c; g_asc = ascending;
    qsort(idx, d->nrows, sizeof *idx, cmp_rows);
    o = take(d, idx, d->nrows); free(idx);
    return o;
}
rxdf *rxdf_select(const rxdf *d, const char *names_csv)
{
    rxdf *o = rxdf_new(); const char *p = names_csv; long r;
    for (;;) {
        const char *e = strchr(p, ','); size_t l = e ? (size_t)(e - p) : strlen(p); char name[256]; long c;
        if (l >= sizeof name) l = sizeof name - 1;
        memcpy(name, p, l); name[l] = 0;
        c = rxdf_find(d, name);
        if (c < 0) { rxdf_release(o); return (rxdf *)dfail("no such column"); }
        if (d->isstr[c]) { char **v = (char **)malloc((d->nrows ? d->nrows : 1) * sizeof *v); for (r = 0; r < d->nrows; r++) v[r] = sdup(d->str[c][r]); add_str_raw(o, name, v, d->nrows); }
        else { double *v = (double *)malloc((d->nrows ? d->nrows : 1) * sizeof *v); memcpy(v, d->num[c], d->nrows * sizeof *v); add_num_raw(o, name, v, d->nrows, d->isint[c]); }
        if (!e) break;
        p = e + 1;
    }
    return o;
}

/* ------------------------------------------------------------------ groupby */
static int key_equal(const rxdf *d, long c, long a, long b)
{ return d->isstr[c] ? !strcmp(d->str[c][a], d->str[c][b]) : d->num[c][a] == d->num[c][b]; }
rxdf *rxdf_groupby(const rxdf *d, const char *key, const char *val, const char *agg)
{
    long kc = rxdf_find(d, key), vc = rxdf_find(d, val), r, g, ng = 0, *idx, *start, *cnt; rxdf *o; char name[300];
    double *res; int op;
    if (kc < 0 || vc < 0) return (rxdf *)dfail("no such column");
    if (d->isstr[vc]) return (rxdf *)dfail("value column must be numeric");
    if (!strcmp(agg, "sum")) op = 0; else if (!strcmp(agg, "mean")) op = 1; else if (!strcmp(agg, "count")) op = 2;
    else if (!strcmp(agg, "min")) op = 3; else if (!strcmp(agg, "max")) op = 4; else if (!strcmp(agg, "std")) op = 5;
    else return (rxdf *)dfail("agg must be sum, mean, count, min, max or std");
    idx = (long *)malloc((d->nrows ? d->nrows : 1) * sizeof *idx);
    for (r = 0; r < d->nrows; r++) idx[r] = r;
    g_sortd = d; g_sortc = kc; g_asc = 1; qsort(idx, d->nrows, sizeof *idx, cmp_rows);   /* sorted keys like groupby(sort=True) */
    start = (long *)malloc((d->nrows + 2) * sizeof *start); cnt = (long *)malloc((d->nrows + 2) * sizeof *cnt);
    for (r = 0; r < d->nrows; r++) {
        if (r == 0 || !key_equal(d, kc, idx[r - 1], idx[r])) { start[ng] = r; ng++; }
    }
    start[ng] = d->nrows;
    res = (double *)malloc((ng ? ng : 1) * sizeof *res);
    for (g = 0; g < ng; g++) {
        long n = 0, i; double s = 0, mn = 0, mx = 0, m, ss = 0;
        for (i = start[g]; i < start[g + 1]; i++) {
            double x = d->num[vc][idx[i]];
            if (x != x) continue;                          /* NaN skipped like pandas */
            if (n == 0) mn = mx = x; else { if (x < mn) mn = x; if (x > mx) mx = x; }
            s += x; n++;
        }
        cnt[g] = n; m = n ? s / n : nanv();
        if (op == 5) { for (i = start[g]; i < start[g + 1]; i++) { double x = d->num[vc][idx[i]]; if (x == x) ss += (x - m) * (x - m); } }
        switch (op) { case 0: res[g] = s; break; case 1: res[g] = m; break; case 2: res[g] = (double)n; break;
                      case 3: res[g] = n ? mn : nanv(); break; case 4: res[g] = n ? mx : nanv(); break;
                      default: res[g] = n > 1 ? sqrt(ss / (n - 1)) : nanv(); break; }
    }
    o = rxdf_new();
    if (d->isstr[kc]) { char **v = (char **)malloc((ng ? ng : 1) * sizeof *v); for (g = 0; g < ng; g++) v[g] = sdup(d->str[kc][idx[start[g]]]); add_str_raw(o, key, v, ng); }
    else { double *v = (double *)malloc((ng ? ng : 1) * sizeof *v); for (g = 0; g < ng; g++) v[g] = d->num[kc][idx[start[g]]]; add_num_raw(o, key, v, ng, d->isint[kc]); }
    (void)name;
    add_num_raw(o, val, res, ng, op == 2 || (op != 1 && op != 5 && d->isint[vc]));
    o->nrows = ng;
    free(idx); free(start); free(cnt);
    return o;
}

/* ------------------------------------------------------------------ merge */
rxdf *rxdf_merge(const rxdf *a, const rxdf *b, const char *key, const char *how)
{
    long ka = rxdf_find(a, key), kb = rxdf_find(b, key), i, j, n = 0, cap = 16, c; long *ia, *ib; int left; rxdf *o;
    if (ka < 0 || kb < 0) return (rxdf *)dfail("key column missing in one of the tables");
    if (a->isstr[ka] != b->isstr[kb]) return (rxdf *)dfail("key columns have different types");
    if (!strcmp(how, "inner")) left = 0; else if (!strcmp(how, "left")) left = 1; else return (rxdf *)dfail("how must be inner or left");
    ia = (long *)malloc(cap * sizeof *ia); ib = (long *)malloc(cap * sizeof *ib);
    for (i = 0; i < a->nrows; i++) {
        int matched = 0;
        for (j = 0; j < b->nrows; j++) {
            int eq = a->isstr[ka] ? !strcmp(a->str[ka][i], b->str[kb][j]) : a->num[ka][i] == b->num[kb][j];
            if (eq) {
                if (n == cap) { cap *= 2; ia = (long *)realloc(ia, cap * sizeof *ia); ib = (long *)realloc(ib, cap * sizeof *ib); }
                ia[n] = i; ib[n] = j; n++; matched = 1;
            }
        }
        if (!matched && left) {
            if (n == cap) { cap *= 2; ia = (long *)realloc(ia, cap * sizeof *ia); ib = (long *)realloc(ib, cap * sizeof *ib); }
            ia[n] = i; ib[n] = -1; n++;
        }
    }
    {
        rxdf *ta = take(a, ia, n), *tb = take(b, ib, n); o = rxdf_new();
        for (c = 0; c < ta->ncols; c++) {
            char nm[300]; long other = rxdf_find(tb, ta->names[c]);
            strcpy(nm, ta->names[c]);
            if (other >= 0 && strcmp(nm, key)) strcat(nm, "_x");
            if (ta->isstr[c]) { char **v = (char **)malloc((n ? n : 1) * sizeof *v); for (i = 0; i < n; i++) v[i] = sdup(ta->str[c][i]); add_str_raw(o, nm, v, n); }
            else { double *v = (double *)malloc((n ? n : 1) * sizeof *v); memcpy(v, ta->num[c], n * sizeof *v); add_num_raw(o, nm, v, n, ta->isint[c]); }
        }
        for (c = 0; c < tb->ncols; c++) {
            char nm[300];
            if (!strcmp(tb->names[c], key)) continue;
            strcpy(nm, tb->names[c]);
            if (rxdf_find(ta, nm) >= 0) strcat(nm, "_y");
            if (tb->isstr[c]) { char **v = (char **)malloc((n ? n : 1) * sizeof *v); for (i = 0; i < n; i++) v[i] = sdup(tb->str[c][i]); add_str_raw(o, nm, v, n); }
            else { double *v = (double *)malloc((n ? n : 1) * sizeof *v); memcpy(v, tb->num[c], n * sizeof *v); add_num_raw(o, nm, v, n, tb->isint[c]); }
        }
        o->nrows = n;
        rxdf_release(ta); rxdf_release(tb);
    }
    free(ia); free(ib);
    return o;
}

/* ------------------------------------------------------------------ describe */
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
rxdf *rxdf_describe(const rxdf *d)
{
    static const char *labels = "count,mean,std,min,25%,50%,75%,max"; rxdf *o = rxdf_new(); long c, r;
    rxdf_add_str(o, "stat", labels);
    for (c = 0; c < d->ncols; c++) {
        double *v, *s, st[8], sum = 0, ss = 0, m; long n = 0; int q;
        if (d->isstr[c]) continue;
        s = (double *)malloc((d->nrows ? d->nrows : 1) * sizeof *s);
        for (r = 0; r < d->nrows; r++) if (d->num[c][r] == d->num[c][r]) { s[n++] = d->num[c][r]; sum += d->num[c][r]; }
        m = n ? sum / n : nanv();
        for (r = 0; r < n; r++) ss += (s[r] - m) * (s[r] - m);
        qsort(s, n, sizeof *s, cmpd);
        st[0] = (double)n; st[1] = m; st[2] = n > 1 ? sqrt(ss / (n - 1)) : nanv();
        st[3] = n ? s[0] : nanv(); st[7] = n ? s[n - 1] : nanv();
        for (q = 0; q < 3; q++) {
            double pos = (n - 1) * (0.25 * (q + 1)); long lo = (long)floor(pos);
            st[4 + q] = n ? (lo + 1 < n ? s[lo] + (s[lo + 1] - s[lo]) * (pos - lo) : s[lo]) : nanv();
        }
        v = (double *)malloc(8 * sizeof *v); memcpy(v, st, sizeof st);
        add_num_raw(o, d->names[c], v, 8, 0);
        free(s);
    }
    return o;
}
