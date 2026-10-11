def div(a, b):
    try:
        return a / b
    except ZeroDivisionError as e:
        print("zde", e)
        return None
    finally:
        print("fin")
print(div(1, 2), div(1, 0))
try:
    int("abc")
except ValueError as e:
    print("ValueError", e)
try:
    [1][5]
except IndexError as e:
    print("IndexError", e)
try:
    {}["k"]
except KeyError as e:
    print("KeyError", e)
try:
    undefined_name
except NameError as e:
    print("NameError", e)
try:
    1 + "a"
except TypeError as e:
    print("TypeError")
class MyErr(Exception):
    def __init__(self, code):
        super().__init__("code " + str(code))
        self.code = code
try:
    raise MyErr(42)
except (KeyError, MyErr) as e:
    print(type(e).__name__, e, e.code, e.args)
try:
    try:
        raise ValueError("inner")
    except ValueError:
        raise RuntimeError("outer")
except Exception as e:
    print(e)
def f():
    for i in range(5):
        try:
            if i == 1: continue
            if i == 3: return i
        finally:
            print("f", i)
print(f())
try:
    raise Exception("plain")
except Exception as e:
    print(repr(e), str(e))
else:
    print("no")
try:
    pass
except Exception:
    print("no")
else:
    print("else ran")
try:
    assert 1 == 2, "assertion msg"
except AssertionError as e:
    print("AE", e)
try:
    try:
        1 / 0
    finally:
        print("cleanup")
except ZeroDivisionError:
    print("caught outer")
try:
    next(iter([]))
except StopIteration:
    print("stop")
print(isinstance(ValueError("x"), Exception), issubclass(KeyError, LookupError))
raise SystemExit(0)
