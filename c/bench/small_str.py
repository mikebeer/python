words = {}
for i in range(5000):
    w = "w" + str(i % 500)
    words[w] = words.get(w, 0) + 1
print(len(words), words["w42"])
s = ",".join(str(i) for i in range(2000))
print(len(s), len(s.split(",")), s.count("9"))
