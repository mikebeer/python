#!/usr/bin/env python3
import numpy as np, pandas as pd, os
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
def labels(df): return ','.join(str(v) for v in df.index)
df = pd.DataFrame({"id": ["r1","r2","r3","r4","r5","r6"], "region": list("EWEWEN"), "prod": list("aabbab"),
                   "units": [5, 3, 2, 8, 7, 1], "price": [1.5, 2.5, 3.0, 0.5, 1.0, 4.0]})
show("gb2", df.groupby(["region", "prod"], as_index=False)["units"].sum())
show("gb2mean", df.groupby(["region", "prod"], as_index=False)["price"].mean())
show("sort2", df.sort_values(["region", "units"], ascending=[True, False], kind="stable"))
show("sort3", df.sort_values(["prod", "price"], ascending=[False, True], kind="stable"))
mg = pd.DataFrame({"region": ["E", "W", "S"], "mgr": [10.0, 20.0, 30.0]})
sm = df[["region", "units"]]
for how in ["inner", "left", "right", "outer"]:
    show("m_" + how, sm.merge(mg, on="region", how=how))
show("vc", df["region"].value_counts().reset_index())
show("vc_prod", df["prod"].value_counts().reset_index())
show("pivot", df.pivot_table(index="region", columns="prod", values="units", aggfunc="sum").reset_index())
show("pivot_mean", df.pivot_table(index="region", columns="prod", values="price", aggfunc="mean").reset_index())
o.append("range_index %s 3" % labels(df))
o.append("filter_index %s" % labels(df[df.units > 3]))
o.append("sorted_index %s" % labels(df.sort_values("units", kind="stable")))
ix = df.set_index("id")
o.append("set_index %s %d %d -1" % (labels(ix), ix.shape[1], ix.index.get_loc("r3")))
o.append("loc_value %d" % int(ix.loc["r4", "units"]))
o.append("sorted_labelled %s" % labels(ix.sort_values("units", ascending=False, kind="stable")))
show("reset", ix.reset_index())
o.append("reset_drop %d %s" % (ix.reset_index(drop=True).shape[1], labels(ix.reset_index(drop=True))))
dt = pd.DataFrame({"when": ["2024-01-31", "2023-12-25 13:45:10", "2024-02-29T23:59:59", "1999-12-31", "2000-03-01 00:00:00", "2024-07-04"]})
t = pd.to_datetime(dt["when"], format="ISO8601")
dt["ts"] = (t - pd.Timestamp("1970-01-01")).dt.total_seconds()
dt["year"] = t.dt.year.astype("int64"); dt["month"] = t.dt.month.astype("int64"); dt["day"] = t.dt.day.astype("int64")
dt["dow"] = t.dt.dayofweek.astype("int64"); dt["doy"] = t.dt.dayofyear.astype("int64")
dt["hour"] = t.dt.hour.astype("int64"); dt["second"] = t.dt.second.astype("int64")
show("datetime", dt[["ts", "year", "month", "day", "dow", "doy", "hour", "second"]])
ts = (pd.Timestamp("2000-01-01") - pd.Timestamp("1970-01-01")).total_seconds()
o.append("scalar %s %d %d" % (int(ts) if ts == int(ts) else ts, pd.Timestamp(951782400, unit="s").dayofweek, pd.Timestamp(951782400, unit="s").dayofyear))
o.append("signal INVALID_ARGUMENTS cannot parse datetime")
open(os.path.join(here, 't_pandas2.expected'), 'w').write('\n'.join(o) + '\n')
