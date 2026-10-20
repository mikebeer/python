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
    char *index_name;      /* name of a labelled index (set_index), else NULL */
    char **index;          /* row labels, NULL = default RangeIndex (0..nrows-1) */
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
/* index */
rxdf *rxdf_set_index(const rxdf *d, const char *col);              /* column -> labelled index (column removed) */
rxdf *rxdf_reset_index(const rxdf *d, int drop);                   /* labels back to RangeIndex (drop=0 keeps them as column "index") */
char *rxdf_index_label(const rxdf *d, long row);                   /* malloc'd label */
long  rxdf_loc(const rxdf *d, const char *label);                  /* first row with this label, -1 if absent */
/* multi-key */
rxdf *rxdf_groupby_multi(const rxdf *d, const char *keys_csv, const char *val, const char *agg);
rxdf *rxdf_sort_multi(const rxdf *d, const char *cols_csv, const char *asc_csv);   /* asc "1,0,..." */
rxdf *rxdf_value_counts(const rxdf *d, const char *col);
rxdf *rxdf_pivot(const rxdf *d, const char *index, const char *columns, const char *values, const char *agg);
/* datetime: ISO "YYYY-MM-DD[ T]HH:MM:SS[.fff][Z]" -> epoch seconds (UTC) */
int   rxdf_parse_datetime(const char *s, double *epoch);
int   rxdf_datetime_part(double epoch, const char *part, long *out);   /* year month day hour minute second dow doy */
int   rxdf_to_datetime(rxdf *d, const char *col, const char *newcol);
int   rxdf_dt_part(rxdf *d, const char *epoch_col, const char *part, const char *newcol);
#endif
