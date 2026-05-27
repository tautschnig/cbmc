# icontract upstream-pattern smoke test.
#
# Distillation of patterns observed in icontract's own test
# suite (tests/test_precondition.py, tests/test_postcondition.py,
# tests/test_snapshot.py, tests/test_invariant.py,
# tests/test_inheritance_*.py). The upstream tests use
# unittest.TestCase + assertRaises(ViolationError) which
# aren't directly verifiable under CBMC's static model;
# this test mirrors the contract shapes directly.

import icontract


# === Precondition with positional arg ===
@icontract.require(lambda x: x > 3)
def positional(x: int, y: int) -> int:
    return x + y


# === Precondition with description ===
@icontract.require(lambda x: x > 3, "x must be greater than three")
def with_description(x: int) -> int:
    return x * 2


# === Multi-clause AND-composed precondition ===
@icontract.require(lambda x: x > 0)
@icontract.require(lambda x: x < 100)
def double_pre(x: int) -> int:
    return x * 2


# === Postcondition with result + parameter ===
@icontract.require(lambda x: x > 0)
@icontract.ensure(lambda result, x: result > x)
def stricter_postcondition(x: int) -> int:
    return x + 1


# === Multi-clause AND-composed postcondition ===
@icontract.require(lambda x: x >= 0)
@icontract.require(lambda x: x <= 50)
@icontract.ensure(lambda result: result >= 0)
@icontract.ensure(lambda result: result <= 100)
def squared_clamped(x: int) -> int:
    if x > 10:
        return 100
    return x * x


# === Snapshot pattern ===
_z = 0


@icontract.snapshot(lambda: _z, name="orig_z")
@icontract.ensure(lambda OLD, val: _z == OLD.orig_z + val)
def add_to_z(val: int) -> None:
    global _z
    _z = _z + val


# === Class invariant ===
@icontract.invariant(lambda self: self.balance >= 0)
class Account:
    def __init__(self, start: int) -> None:
        self.balance = start

    @icontract.require(lambda amount: amount > 0)
    def deposit(self, amount: int) -> None:
        self.balance = self.balance + amount


# === Inheritance: child class has its own invariant ===
@icontract.invariant(lambda self: self.balance <= 1000)
class CappedAccount(Account):
    pass  # inherits Account.deposit; both invariants apply


# === Method ensure binding result ===
class Calculator:
    @icontract.require(lambda x: x >= 0)
    @icontract.ensure(lambda result, x: result == x * x)
    def square(self, x: int) -> int:
        return x * x


def main():
    # Precondition + result composition.
    assert positional(5, 7) == 12
    assert with_description(4) == 8
    assert double_pre(50) == 100
    assert stricter_postcondition(10) == 11
    assert squared_clamped(5) == 25
    assert squared_clamped(20) == 100

    # Snapshot + module state.
    add_to_z(10)
    assert _z == 10
    add_to_z(15)
    assert _z == 25

    # Class invariant + per-method require.
    a = Account(100)
    a.deposit(50)
    assert a.balance == 150

    # Inheritance: child invariant + inherited method.
    cap = CappedAccount(100)
    cap.deposit(200)  # within cap
    assert cap.balance == 300

    # Method on a class with @ensure referencing result.
    c = Calculator()
    assert c.square(7) == 49


main()
