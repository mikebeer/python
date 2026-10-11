# json benchmark; "py json_bench.py pure" runs the pure-Python pylib/json.py (copied to pyjson.py by run.sh)
import sys
if len(sys.argv) > 1 and sys.argv[1] == "pure":
    import pyjson as json
else:
    import json

data = []
for i in range(300):
    data.append({"id": i, "name": "item %d" % i, "tags": ["a", "b\n", "cé"], "price": i * 1.25,
                 "ok": i % 2 == 0, "none": None, "nested": {"x": [1, 2, 3], "y": {"z": "w" * 20}}})
total = 0
for i in range(30):
    s = json.dumps(data)
    total += len(s)
    total += len(json.dumps(data, indent=2, sort_keys=True))
    back = json.loads(s)
    total += len(back)
    total += len(json.loads(json.dumps(data, ensure_ascii=False)))
print(total)
