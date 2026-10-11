# regex benchmark; "py re_bench.py pure" runs the pure-Python pylib/re.py (copied to pyre.py by run.sh)
import sys
if len(sys.argv) > 1 and sys.argv[1] == "pure":
    import pyre as re
else:
    import re

text = ("The quick brown fox jumps over the lazy dog 12345 times; "
        "mail bob@example.com or alice.smith@mail.example.org on 2024-05-17.\n") * 400
total = 0
for i in range(20):
    total += len(re.findall(r"\w+", text))
    total += len(re.findall(r"\d{4}-\d{2}-\d{2}", text))
    total += len(re.findall(r"[\w.]+@[\w.]+\.\w+", text))
    total += len(re.sub(r"\s+", " ", text))
    total += len(re.split(r"[;.\n]\s*", text))
    total += len(re.findall(r"(?i)(?:the|dog)\b", text))
    for m in re.finditer(r"(?P<y>\d{4})-(?P<m>\d\d)", text):
        total += int(m.group("y")) % 7
    total += len(re.sub(r"(\w+)@(\w+)", r"\2 at \1", text))
words = text.split()
pat = re.compile(r"^[a-z]+$")
for i in range(10):
    for w in words:
        if pat.match(w):
            total += 1
print(total)
