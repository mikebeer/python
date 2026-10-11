class Animal:
    count = 0
    def __init__(self, name, sound="..."):
        self.name = name
        self.sound = sound
        Animal.count += 1
    def speak(self):
        return self.name + " says " + self.sound
    def __str__(self):
        return "Animal(" + self.name + ")"
    def __repr__(self):
        return "<A " + self.name + ">"
class Dog(Animal):
    def __init__(self, name):
        super().__init__(name, "woof")
        self.tricks = []
    def add(self, t):
        self.tricks.append(t)
        return self
    def speak(self):
        return Animal.speak(self) + "!"
d = Dog("Rex")
a = Animal("Generic")
print(d.speak(), a.speak(), Animal.count, str(d), repr(d), [d, a])
print(d.add("sit").add("roll").tricks, isinstance(d, Animal), isinstance(a, Dog), issubclass(Dog, Animal))
print(type(d).__name__, d.__class__.__name__, hasattr(d, "name"), hasattr(d, "zzz"), getattr(d, "name"), getattr(d, "q", 9))
setattr(d, "age", 3); print(d.age)
class Vec:
    def __init__(self, x, y):
        self.x, self.y = x, y
    def __add__(self, o): return Vec(self.x + o.x, self.y + o.y)
    def __sub__(self, o): return Vec(self.x - o.x, self.y - o.y)
    def __mul__(self, k): return Vec(self.x * k, self.y * k)
    def __eq__(self, o): return self.x == o.x and self.y == o.y
    def __neg__(self): return Vec(-self.x, -self.y)
    def __len__(self): return 2
    def __getitem__(self, i): return (self.x, self.y)[i]
    def __bool__(self): return self.x != 0 or self.y != 0
    def __repr__(self): return "Vec(%d, %d)" % (self.x, self.y)
    def __lt__(self, o): return (self.x, self.y) < (o.x, o.y)
v = Vec(1, 2) + Vec(3, 4)
print(v, v * 2, -v, v - Vec(1, 1), v == Vec(4, 6), v != Vec(0, 0), len(v), v[1], bool(Vec(0, 0)), sorted([Vec(2, 1), Vec(1, 5)]))
class Stack:
    def __init__(self): self._items = []
    def push(self, x): self._items.append(x)
    def pop(self): return self._items.pop()
    def __iter__(self): return iter(self._items)
    def __contains__(self, x): return x in self._items
    @property
    def size(self): return len(self._items)
    @staticmethod
    def make(): return Stack()
    @classmethod
    def named(cls, n): s = cls(); s.n = n; return s
s = Stack.make(); s.push(1); s.push(2)
print(list(s), 2 in s, s.size, Stack.named("q").n)
class Base:
    def hello(self): return "base " + self.who()
    def who(self): return "B"
class Mid(Base):
    def who(self): return "M"
class Leaf(Mid):
    pass
print(Leaf().hello(), Leaf.__name__, Leaf.__mro__[1].__name__ if False else "Mid")
class Temp:
    def __init__(self): self._c = 0
    def get(self): return self._c
    def __call__(self, x): return x + self._c
    def __hash__(self): return 7
    def __enter__(self): print("enter"); return self
    def __exit__(self, *a): print("exit"); return False
with Temp() as t:
    print(t(5))
