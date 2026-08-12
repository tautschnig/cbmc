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


def _pow10(n: int) -> int:
    # Integer 10**n for n >= 0. Used instead of `10 ** n`, which Python
    # types as float (since ** yields float for negative exponents) and
    # would route the exact integer arithmetic through floatbv. Bounded
    # by the unwind depth (the exponent-alignment distance); see the
    # decimal plan's soundness notes.
    r = 1
    i = 0
    while i < n:
        r = r * 10
        i = i + 1
    return r


def _rescale_coeff(coeff: int, exp: int, target: int) -> int:
    # Round the unsigned coefficient of value (coeff * 10**exp) to the
    # target exponent using ROUND_HALF_EVEN (the decimal default), and
    # return the new coefficient (the result's exponent is `target`).
    # Shared by quantize and (after extra guard digits) division/sqrt.
    if target <= exp:
        # Padding with zeros — exact, no rounding.
        return coeff * _pow10(exp - target)
    div = _pow10(target - exp)
    q = coeff // div
    rem = coeff - q * div
    twice = rem * 2
    if twice > div:
        q = q + 1
    elif twice == div:
        # Tie: round to even.
        if q % 2 == 1:
            q = q + 1
    return q


class Decimal:
    # Exact base-10 model: value = (-1)**_sign * _int * 10**_exp.
    # See doc/python-frontend-decimal-plan.md. String/int *literals* are
    # parsed by the converter (build_decimal_literal) straight into this
    # struct; this __init__ covers the non-literal paths. The class-level
    # annotations declare the instance fields (and their int type) for the
    # frontend's struct layout.
    _sign: int = 0
    _int: int = 0
    _exp: int = 0
    _is_special: int = 0
    _special_kind: int = 0

    def __bool__(self):
        # stdtypes Truth Value Testing / CPython decimal: a Decimal
        # is falsy iff it is (positive or negative) ZERO. Specials
        # (NaN, Infinity) are truthy; zero has coefficient 0.
        return self._is_special != 0 or self._int != 0

    def __init__(self, value=0, context=None):
        if isinstance(value, Decimal):
            self._sign = value._sign
            self._int = value._int
            self._exp = value._exp
            self._is_special = value._is_special
            self._special_kind = value._special_kind
        elif isinstance(value, int):
            self._sign = 1 if value < 0 else 0
            self._int = -value if value < 0 else value
            self._exp = 0
            self._is_special = 0
            self._special_kind = 0
        else:
            # A non-literal value reaching here is a float (Decimal(1.1) —
            # whose exact binary expansion exceeds the 64-bit coefficient)
            # or a symbolic string. Model it as an unconstrained finite
            # Decimal: sound (never fabricates a specific wrong value such
            # as 0, which would let `Decimal(1.1) == 0` false-prove).
            self._sign = nondet_int()
            self._int = nondet_int()
            self._exp = nondet_int()
            self._is_special = 0
            self._special_kind = 0

    def __add__(self, other):
        if self._is_special or other._is_special:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        if self._exp <= other._exp:
            e = self._exp
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - e))
        else:
            e = other._exp
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - e))
            sb = -other._int if other._sign else other._int
        val = sa + sb
        r = Decimal(0)
        r._sign = 1 if val < 0 else 0
        r._int = -val if val < 0 else val
        r._exp = e
        return r

    def __sub__(self, other):
        if self._is_special or other._is_special:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        if self._exp <= other._exp:
            e = self._exp
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - e))
        else:
            e = other._exp
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - e))
            sb = -other._int if other._sign else other._int
        val = sa - sb
        r = Decimal(0)
        r._sign = 1 if val < 0 else 0
        r._int = -val if val < 0 else val
        r._exp = e
        return r

    def __mul__(self, other):
        if self._is_special or other._is_special:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        r = Decimal(0)
        r._sign = 0 if self._sign == other._sign else 1
        r._int = self._int * other._int
        r._exp = self._exp + other._exp
        return r

    def __neg__(self):
        r = Decimal(0)
        r._sign = 0 if self._sign else 1
        r._int = self._int
        r._exp = self._exp
        r._is_special = self._is_special
        r._special_kind = self._special_kind
        return r

    def __abs__(self):
        r = Decimal(0)
        r._sign = 0
        r._int = self._int
        r._exp = self._exp
        r._is_special = self._is_special
        r._special_kind = self._special_kind
        return r

    def __floordiv__(self, other):
        if self._is_special or other._is_special or other._int == 0:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        if self._exp <= other._exp:
            e = self._exp
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - e))
        else:
            e = other._exp
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - e))
            sb = -other._int if other._sign else other._int
        # Truncate toward zero (Decimal // semantics).
        aa = -sa if sa < 0 else sa
        ab = -sb if sb < 0 else sb
        q = aa // ab
        neg = (sa < 0) != (sb < 0)
        r = Decimal(0)
        r._sign = 1 if (neg and q != 0) else 0
        r._int = q
        r._exp = 0
        return r

    def __mod__(self, other):
        if self._is_special or other._is_special or other._int == 0:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        if self._exp <= other._exp:
            e = self._exp
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - e))
        else:
            e = other._exp
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - e))
            sb = -other._int if other._sign else other._int
        aa = -sa if sa < 0 else sa
        ab = -sb if sb < 0 else sb
        q = aa // ab
        neg = (sa < 0) != (sb < 0)
        sq = -q if neg else q
        val = sa - sq * sb
        r = Decimal(0)
        r._sign = 1 if val < 0 else 0
        r._int = -val if val < 0 else val
        r._exp = e
        return r

    def __truediv__(self, other):
        # Exact when the quotient terminates within a bounded number of
        # decimal digits (denominator with only 2/5 factors); otherwise
        # (e.g. 1/3) the exact value needs round-to-context at 28 digits,
        # which exceeds the 64-bit coefficient, so fall back to a sound
        # unconstrained finite Decimal.
        if self._is_special or other._is_special or other._int == 0:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        ca = self._int
        cb = other._int
        q = ca // cb
        rem = ca - q * cb
        extra = 0
        while rem != 0 and extra < 15:
            num = rem * 10
            d = num // cb
            q = q * 10 + d
            rem = num - d * cb
            extra = extra + 1
        if rem != 0:
            # Non-terminating within the bound: conservative nondet.
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            return r
        r = Decimal(0)
        r._sign = (0 if self._sign == other._sign else 1) if q != 0 else 0
        r._int = q
        r._exp = self._exp - other._exp - extra
        return r

    def __eq__(self, other):
        if self._special_kind >= 3 or other._special_kind >= 3:
            return False
        if self._is_special or other._is_special:
            return nondet_bool()
        if self._exp <= other._exp:
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - self._exp))
        else:
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - other._exp))
            sb = -other._int if other._sign else other._int
        return sa == sb

    def __ne__(self, other):
        return not self.__eq__(other)

    def __lt__(self, other):
        if self._special_kind >= 3 or other._special_kind >= 3:
            return False
        if self._is_special or other._is_special:
            return nondet_bool()
        if self._exp <= other._exp:
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - self._exp))
        else:
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - other._exp))
            sb = -other._int if other._sign else other._int
        return sa < sb

    def __le__(self, other):
        if self._special_kind >= 3 or other._special_kind >= 3:
            return False
        if self._is_special or other._is_special:
            return nondet_bool()
        if self._exp <= other._exp:
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - self._exp))
        else:
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - other._exp))
            sb = -other._int if other._sign else other._int
        return sa <= sb

    def __gt__(self, other):
        if self._special_kind >= 3 or other._special_kind >= 3:
            return False
        if self._is_special or other._is_special:
            return nondet_bool()
        if self._exp <= other._exp:
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - self._exp))
        else:
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - other._exp))
            sb = -other._int if other._sign else other._int
        return sa > sb

    def __ge__(self, other):
        if self._special_kind >= 3 or other._special_kind >= 3:
            return False
        if self._is_special or other._is_special:
            return nondet_bool()
        if self._exp <= other._exp:
            sa = -self._int if self._sign else self._int
            sb = (-other._int if other._sign else other._int) * (
                _pow10(other._exp - self._exp))
        else:
            sa = (-self._int if self._sign else self._int) * (
                _pow10(self._exp - other._exp))
            sb = -other._int if other._sign else other._int
        return sa >= sb

    def is_zero(self) -> bool:
        return (not self._is_special) and self._int == 0

    def is_signed(self) -> bool:
        return self._sign == 1

    def is_finite(self) -> bool:
        return self._is_special == 0

    def is_infinite(self) -> bool:
        return self._special_kind == 1 or self._special_kind == 2

    def is_nan(self) -> bool:
        return self._special_kind == 3 or self._special_kind == 4

    def quantize(self, exp, rounding=None, context=None):
        # Round self to the exponent of `exp` using ROUND_HALF_EVEN (the
        # decimal default). `exp` is a Decimal whose _exp is the target.
        if self._is_special or exp._is_special:
            r = Decimal(0)
            r._sign = nondet_int()
            r._int = nondet_int()
            r._exp = nondet_int()
            r._is_special = nondet_int()
            r._special_kind = nondet_int()
            return r
        r = Decimal(0)
        r._sign = self._sign
        r._int = _rescale_coeff(self._int, self._exp, exp._exp)
        r._exp = exp._exp
        return r

    def sqrt(self, context=None):
        # Decimal.sqrt is irrational in general and rounds to context
        # precision (28 digits > 64-bit coefficient), so the result is
        # modelled as a sound unconstrained finite Decimal. (Remaining
        # P3 residual; see doc/python-frontend-decimal-plan.md.)
        r = Decimal(0)
        r._sign = nondet_int()
        r._int = nondet_int()
        r._exp = nondet_int()
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
