#!/usr/bin/env python3
import numpy as np, pandas as pd, os, math
here = os.path.dirname(os.path.abspath(__file__))
def rr(x): return str((np.round(np.asarray(x, dtype=float) * 1e9) / 1e9).tolist())
o = []
def show(label, df):
    o.append("%s rows %d" % (label, len(df)))
    for c in df.columns:
        col = df[c]
        if col.dtype == object or str(col.dtype).startswith('str'):
            o.append("  %s [%s]" % (c, ', '.join("'%s'" % v for v in col)))
        elif col.dtype.kind == 'i': o.append("  %s %s" % (c, str(col.tolist())))
        else: o.append("  %s %s" % (c, rr(col.to_numpy())))
df = pd.DataFrame({"city": list("abacba"), "sales": [10.5, 20.0, 30.25, 5.0, 15.0, 7.5], "qty": [1, 2, 3, 4, 5, 6]})
show("df", df)
show("filter", df[df.sales > 9]); show("filterstr", df[df.city == "b"])
show("sort", df.sort_values("sales", ascending=False, kind="stable"))
show("sortcity", df.sort_values("city", kind="stable"))
g = lambda v, a: df.groupby("city", as_index=False)[v].agg(a)
show("sum", g("sales", "sum")); show("mean", g("sales", "mean")); show("count", g("qty", "count"))
show("qtysum", g("qty", "sum")); show("std", g("sales", "std")); show("max", g("sales", "max"))
rg = pd.DataFrame({"city": ["a", "b", "d"], "factor": [1.5, 2.5, 9.0]})
show("inner", df.merge(rg, on="city", how="inner"))
show("left", df.merge(rg[rg.city != "b"], on="city", how="left"))
d = df.describe().reset_index().rename(columns={"index": "stat"}); show("describe", d)
show("head", df.head(2)); show("select", df[["qty", "city"]])
df.to_csv("/tmp/rxpandas_expected.csv", index=False)
show("csv", pd.read_csv("/tmp/rxpandas_expected.csv"))
o.append("signal INVALID_ARGUMENTS no such column")
open(os.path.join(here, 't_pandas.expected'), 'w').write('\n'.join(o) + '\n')
