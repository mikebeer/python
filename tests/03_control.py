n = 0
while n < 5:
    n += 1
    if n == 2:
        continue
    if n == 4:
        break
    print("n", n)
else:
    print("not reached")
for i in range(3):
    print(i)
else:
    print("for-else")
for i in range(10, 0, -3):
    print(i, end=" ")
print()
for i in range(0):
    print("never")
print(list(range(5)), list(range(2, 8, 2)), len(range(10)), range(5)[2], 3 in range(5))
for a, b in [(1, 2), (3, 4)]:
    print(a + b)
x = 15
if x < 10:
    print("small")
elif x < 20:
    print("medium")
else:
    print("large")
print("yes" if x > 10 else "no")
total = 0
for i in range(1, 11):
    if i % 2:
        continue
    total += i
print(total)
primes = []
for k in range(2, 30):
    for d in range(2, k):
        if k % d == 0:
            break
    else:
        primes.append(k)
print(primes)
def f():
    pass
print(f())
i = 0
while True:
    i += 1
    if i > 3:
        break
print(i)
print(*[1, 2, 3], sep="-")
print("a", "b", sep="", end="!\n")
