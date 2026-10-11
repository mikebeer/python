d = {}
for i in range(200000):
    d[i] = i * 2
s = 0
for k in d:
    s += d[k]
print(s)
l = []
for i in range(300000):
    l.append(i * 3 % 1000)
l.sort()
print(l[0], l[-1], len(set(l)))
words = {}
for i in range(100000):
    w = "w" + str(i % 5000)
    words[w] = words.get(w, 0) + 1
print(len(words), words["w42"])
print(sum([x * x for x in range(200000)]))
print(len({x % 1000: x for x in range(100000)}))
