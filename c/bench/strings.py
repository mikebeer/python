parts = []
for i in range(100000):
    parts.append("item" + str(i))
s = ",".join(parts)
print(len(s))
t = ""
for i in range(20000):
    t += "x"
print(len(t))
words = s.split(",")
print(len(words), words[-1])
u = 0
for w in words:
    if w.endswith("7"):
        u += len(w.upper().replace("ITEM", "i"))
print(u)
print(len("%d-%s" % (5, "a") * 1000), len(f"{12345:>10}" * 5000))
