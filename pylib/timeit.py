# timeit: Timer with stmt/setup callables or source strings (exec of strings needs exec support)
import time as _time

default_timer = _time.perf_counter
dummy_src_name = "<timeit-src>"
default_number = 1000000
default_repeat = 5


class Timer:
    def __init__(self, stmt="pass", setup="pass", timer=default_timer, globals=None):
        self.timer = timer
        self._globals = globals if globals is not None else {}
        if callable(stmt):
            self._stmt = stmt
        else:
            self._stmt = self._compile(stmt)
        if callable(setup):
            self._setup = setup
        else:
            self._setup = self._compile(setup)

    def _compile(self, src):
        g = self._globals

        def run():
            exec(src, g)
        return run

    def timeit(self, number=default_number):
        self._setup()
        stmt = self._stmt
        timer = self.timer
        t0 = timer()
        for _ in range(number):
            stmt()
        return timer() - t0

    def repeat(self, repeat=default_repeat, number=default_number):
        r = []
        for i in range(repeat):
            r.append(self.timeit(number))
        return r

    def autorange(self, callback=None):
        i = 1
        while True:
            for j in 1, 2, 5:
                number = i * j
                time_taken = self.timeit(number)
                if callback:
                    callback(number, time_taken)
                if time_taken >= 0.2:
                    return (number, time_taken)
            i *= 10

    def print_exc(self, file=None):
        pass


def timeit(stmt="pass", setup="pass", timer=default_timer, number=default_number, globals=None):
    return Timer(stmt, setup, timer, globals).timeit(number)


def repeat(stmt="pass", setup="pass", timer=default_timer, repeat=default_repeat, number=default_number, globals=None):
    return Timer(stmt, setup, timer, globals).repeat(repeat, number)
