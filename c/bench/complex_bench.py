z = 0j
c = complex(-0.4, 0.6)
count = 0
for i in range(60):
    for j in range(60):
        c = complex(-2.0 + i * 0.05, -1.5 + j * 0.05)
        z = 0j
        for k in range(30):
            z = z * z + c
            if abs(z) > 2.0:
                break
        else:
            count += 1
print(count)
