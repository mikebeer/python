s = "Hello, World"
print(s, len(s), s.upper(), s.lower(), s[0], s[-1], s[1:5], s[:5], s[7:], s[::-1], s[::2])
print(s.find("World"), s.find("zzz"), s.index("o"), s.count("l"), s.startswith("He"), s.endswith("d"))
print(s.replace("l", "L"), s.replace("l", "L", 1), "a,b,c".split(","), "  a  b ".split(), " x ".strip(), " x ".lstrip(), " x ".rstrip())
print("-".join(["a", "b", "c"]), "ab" * 3, "a" + "b", "abc" in "xabcx", "z" not in "abc")
print("abc".isalpha(), "123".isdigit(), "a1".isalnum(), "  ".isspace(), "Abc".isupper(), "abc".islower())
print("title case here".title(), "hello".capitalize(), "Hello".swapcase(), "x".center(5, "*"), "x".ljust(3, "."), "x".rjust(3, "."), "7".zfill(3))
name = "Bob"
age = 30
print(f"{name} is {age} years old", f"{age + 1}", f"{name!r}", f"{age:5d}|", f"{age:<5d}|", f"{3.14159:.2f}", f"{255:x}", f"{255:08b}", f"{1234567:,}")
print("%s is %d" % ("Al", 5), "%5.2f|%-5d|%05d" % (3.14159, 42, 42), "%x %o %e" % (255, 8, 12345.678))
print("{} {}".format("a", "b"), "{1} {0}".format("a", "b"), "{name}".format(name="z"), "{:>6}".format("r"), "{:^7}".format("mid"))
print(repr("it's"), repr('say "hi"'), repr("a\nb\t"), str(12), str(1.5), str([1, "a"]), str(None))
print("a\tb", "q\\z", 'single', """triple
quoted""", r"raw\n")
print(ord("A"), chr(66), "abc" < "abd", "abc".partition("b"), "a=b=c".rpartition("="), "a b c".rsplit(" ", 1))
print("line1\nline2\n".splitlines(), "x".join("abc"), list("abc"), sorted("cba"), "abc".encode()[0] if False else 97)
for i, c in enumerate("ab"):
    print(i, c)
