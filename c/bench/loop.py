total = 0
i = 0
while i < 3000000:
    total = (total + i * 3 - (i >> 1)) % 1000003
    i += 1
print(total)
x = 0
for j in range(1000000):
    x += j % 7
print(x)
