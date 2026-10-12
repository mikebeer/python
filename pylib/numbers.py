# numbers: the numeric tower as plain classes (int/float/complex are not registered: isinstance(1, Number) is False)
from abc import ABC

__all__ = ["Number", "Complex", "Real", "Rational", "Integral"]


class Number(ABC):
    __hash__ = None


class Complex(Number):
    def __complex__(self):
        raise NotImplementedError

    def __bool__(self):
        return self != 0

    @property
    def real(self):
        raise NotImplementedError

    @property
    def imag(self):
        raise NotImplementedError

    def __neg__(self):
        raise NotImplementedError

    def __pos__(self):
        raise NotImplementedError

    def __sub__(self, other):
        return self + -other

    def __rsub__(self, other):
        return -self + other

    def conjugate(self):
        raise NotImplementedError

    def __abs__(self):
        raise NotImplementedError


class Real(Complex):
    def __float__(self):
        raise NotImplementedError

    def __divmod__(self, other):
        return (self // other, self % other)

    def __rdivmod__(self, other):
        return (other // self, other % self)

    def __complex__(self):
        return complex(float(self))

    @property
    def real(self):
        return +self

    @property
    def imag(self):
        return 0

    def conjugate(self):
        return +self


class Rational(Real):
    @property
    def numerator(self):
        raise NotImplementedError

    @property
    def denominator(self):
        raise NotImplementedError

    def __float__(self):
        return int(self.numerator) / int(self.denominator)


class Integral(Rational):
    def __int__(self):
        raise NotImplementedError

    def __index__(self):
        return int(self)

    def __float__(self):
        return float(int(self))

    @property
    def numerator(self):
        return +self

    @property
    def denominator(self):
        return 1
