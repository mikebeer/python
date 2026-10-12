d = {}
for i in range(10000):
    d[i] = i * 2
s = 0
for k in d:
    s += d[k]
print(s)
l = []
for i in range(10000):
    l.append(i * 3 % 1000)
l.sort()
print(l[0], l[-1], len(set(l)))
print(sum([x * x for x in range(10000)]))
