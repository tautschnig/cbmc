# Comprehensive icontract integration test covering Phases
# 1-6: library stub, @require, @ensure (with `result`),
# @snapshot + OLD, and @invariant on classes.

import icontract


# --- Function-level contracts ---

@icontract.require(lambda x: x >= 0)
@icontract.ensure(lambda result, x: result == x * x)
def square(x: int) -> int:
    return x * x


@icontract.require(lambda x, y: x >= 0 and y >= 0)
@icontract.ensure(lambda result, x, y: result >= x)
@icontract.ensure(lambda result, x, y: result >= y)
def maximum(x: int, y: int) -> int:
    if x > y:
        return x
    return y


# --- Snapshot + OLD ---

_total = 0


@icontract.snapshot(lambda: _total, name="prev")
@icontract.ensure(lambda OLD, n: _total == OLD.prev + n)
def add_to_total(n: int) -> None:
    global _total
    _total = _total + n


# --- Class-level invariant ---

@icontract.invariant(lambda self: self.balance >= 0)
@icontract.invariant(lambda self: self.history_count >= 0)
class Account:
    def __init__(self, start: int) -> None:
        self.balance = start
        self.history_count = 0

    @icontract.require(lambda amt: amt > 0)
    def deposit(self, amt: int) -> None:
        self.balance = self.balance + amt
        self.history_count = self.history_count + 1

    @icontract.require(lambda self, amt: amt > 0 and amt <= self.balance)
    @icontract.ensure(lambda result, amt: result == amt)
    def withdraw(self, amt: int) -> int:
        self.balance = self.balance - amt
        self.history_count = self.history_count + 1
        return amt


def main():
    # Function contracts.
    s = square(5)
    assert s == 25

    m = maximum(7, 3)
    assert m == 7

    # Snapshot.
    add_to_total(10)
    assert _total == 10
    add_to_total(7)
    assert _total == 17

    # Class invariants.
    a = Account(100)
    a.deposit(50)
    assert a.balance == 150

    # withdraw with valid amount keeps the invariant.
    taken = a.withdraw(40)
    assert taken == 40
    assert a.balance == 110
    assert a.history_count == 2


main()
