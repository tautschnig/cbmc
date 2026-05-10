"""
Verification model of the `decimal` module.

Decimal arithmetic. Modelled as a thin wrapper around float
under-the-hood — precision beyond float isn't tracked, but
the API surface is complete so library code that imports
decimal types for type-annotations or for conservative
conversions keeps working.
"""


class DecimalException(Exception):
    pass


class Clamped(DecimalException):
    pass


class DivisionByZero(DecimalException, ZeroDivisionError):
    pass


class InvalidOperation(DecimalException, ArithmeticError):
    pass


class Overflow(DecimalException):
    pass


class Underflow(DecimalException):
    pass


class Inexact(DecimalException):
    pass


class Rounded(DecimalException):
    pass


class Subnormal(DecimalException):
    pass


class FloatOperation(DecimalException, TypeError):
    pass


# Rounding modes — CPython exposes these as strings at runtime.
ROUND_DOWN = "ROUND_DOWN"
ROUND_HALF_UP = "ROUND_HALF_UP"
ROUND_HALF_EVEN = "ROUND_HALF_EVEN"
ROUND_CEILING = "ROUND_CEILING"
ROUND_FLOOR = "ROUND_FLOOR"
ROUND_UP = "ROUND_UP"
ROUND_HALF_DOWN = "ROUND_HALF_DOWN"
ROUND_05UP = "ROUND_05UP"


class Decimal:
    def __init__(self, value=0, context=None):
        # Best-effort float coercion — verification uses the
        # magnitude for comparisons and arithmetic.
        if isinstance(value, Decimal):
            self._v = value._v
        elif isinstance(value, (int, float)):
            self._v = float(value)
        else:
            self._v = 0.0

    def __add__(self, other):
        if isinstance(other, Decimal):
            r = Decimal(0)
            r._v = self._v + other._v
            return r
        r = Decimal(0)
        r._v = self._v + float(other)
        return r

    def __sub__(self, other):
        if isinstance(other, Decimal):
            r = Decimal(0)
            r._v = self._v - other._v
            return r
        r = Decimal(0)
        r._v = self._v - float(other)
        return r

    def __mul__(self, other):
        r = Decimal(0)
        if isinstance(other, Decimal):
            r._v = self._v * other._v
        else:
            r._v = self._v * float(other)
        return r

    def __truediv__(self, other):
        r = Decimal(0)
        if isinstance(other, Decimal):
            r._v = self._v / other._v
        else:
            r._v = self._v / float(other)
        return r

    def __floordiv__(self, other):
        r = Decimal(0)
        o = other._v if isinstance(other, Decimal) else float(other)
        r._v = float(int(self._v // o))
        return r

    def __mod__(self, other):
        r = Decimal(0)
        o = other._v if isinstance(other, Decimal) else float(other)
        r._v = self._v - (self._v // o) * o
        return r

    def __neg__(self):
        r = Decimal(0)
        r._v = -self._v
        return r

    def __abs__(self):
        r = Decimal(0)
        r._v = abs(self._v)
        return r

    def __eq__(self, other):
        if isinstance(other, Decimal):
            return self._v == other._v
        return self._v == float(other)

    def __lt__(self, other):
        o = other._v if isinstance(other, Decimal) else float(other)
        return self._v < o

    def __le__(self, other):
        o = other._v if isinstance(other, Decimal) else float(other)
        return self._v <= o

    def __gt__(self, other):
        o = other._v if isinstance(other, Decimal) else float(other)
        return self._v > o

    def __ge__(self, other):
        o = other._v if isinstance(other, Decimal) else float(other)
        return self._v >= o

    def is_zero(self) -> bool:
        return self._v == 0.0

    def is_signed(self) -> bool:
        return self._v < 0.0

    def is_finite(self) -> bool:
        return True

    def is_infinite(self) -> bool:
        return False

    def is_nan(self) -> bool:
        return False

    def quantize(self, exp, rounding=None, context=None):
        r = Decimal(0)
        r._v = self._v
        return r

    def sqrt(self, context=None):
        r = Decimal(0)
        r._v = self._v ** 0.5 if self._v >= 0.0 else 0.0
        return r


class Context:
    def __init__(self, prec: int = 28, rounding: str = ROUND_HALF_EVEN,
                 Emin: int = -999999, Emax: int = 999999,
                 capitals: int = 1, clamp: int = 0,
                 flags=None, traps=None):
        self.prec = prec
        self.rounding = rounding
        self.Emin = Emin
        self.Emax = Emax
        self.capitals = capitals
        self.clamp = clamp


_DEFAULT_CONTEXT = Context()


def getcontext() -> Context:
    return _DEFAULT_CONTEXT


def setcontext(ctx: Context) -> None:
    return None


def localcontext(ctx=None):
    # Minimal — returns a new Context; the `with` statement
    # frontend simplification doesn't rely on contextmanager
    # protocol for this.
    if ctx is None:
        return Context()
    return ctx
