/* rxdf - C89 column-oriented table (a small pandas.DataFrame subset). Columns are float64 (optionally
 * flagged integer for output) or string.  No index: rows are positional (reset_index semantics). */
#ifndef RXDF_H
#define RXDF_H
#include "rxnp.h"
typedef struct rxdf {
    long refs, nrows, ncols;
    char **names;
    int *isstr;            /* 1 = string column */
    int *isint;            /* numeric column holds integers (output as ints) */
    double **num;          /* num[c] valid when !isstr[c] */
    char ***str;           /* str[c][r] valid when isstr[c] */
} rxdf;
const char *rxdf_error(void);
rxdf *rxdf_new(void);
rxdf *rxdf_retain(rxdf *d);
void  rxdf_release(rxdf *d);
long  rxdf_find(const rxdf *d, const char *name);                     /* -1 if absent */
int   rxdf_add_num(rxdf *d, const char *name, const rxnp *a);         /* copies; length must match */
int   rxdf_add_str(rxdf *d, const char *name, const char *csv_values);/* comma separated */
rxdf *rxdf_read_csv(const char *path);
rxdf *rxdf_parse_csv(const char *text);
int   rxdf_to_csv(const rxdf *d, const char *path);
char *rxdf_to_text(const rxdf *d, long maxrows);                      /* malloc'd, aligned print */
rxnp *rxdf_col(const rxdf *d, const char *name);                      /* numeric column -> 1-d array */
rxdf *rxdf_filter(const rxdf *d, const char *col, const char *op, const char *value);
rxdf *rxdf_sort(const rxdf *d, const char *col, int ascending);
rxdf *rxdf_head(const rxdf *d, long n);
rxdf *rxdf_groupby(const rxdf *d, const char *key, const char *val, const char *agg);
rxdf *rxdf_merge(const rxdf *a, const rxdf *b, const char *key, const char *how);
rxdf *rxdf_describe(const rxdf *d);
rxdf *rxdf_select(const rxdf *d, const char *names_csv);
#endif
