total = 0
i = 0
while i < 100000:
    total = (total + i * 3 - (i >> 1)) % 1000003
    i += 1
print(total)
