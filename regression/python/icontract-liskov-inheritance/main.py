# Phase 7: Liskov inheritance composition for icontract.
#
# Tests the three composition rules at single-level inheritance:
#  1. Class invariants merge: child invariants AND parent's
#     are asserted on every method of the child.
#  2. Method preconditions weaken: effective precondition is
#     `parent_pre OR child_pre` so a child can accept inputs
#     the parent rejected.
#  3. Method postconditions strengthen: child must satisfy
#     BOTH its own ensure and the parent's.
#
# Note: Python class-method-inheritance (calling a parent's
# method on a subclass instance) has a pre-existing limitation
# in the frontend that is orthogonal to icontract. This test
# only invokes methods declared directly on the subclass.

import icontract


# ----- Class invariant inheritance -----

@icontract.invariant(lambda self: self.balance >= 0)
class Account:
    def __init__(self, start: int) -> None:
        self.balance = start


@icontract.invariant(lambda self: self.balance <= 1000000)
class CappedAccount(Account):
    # The parent's invariant (balance >= 0) is composed with
    # this class's invariant (balance <= 1_000_000) — both
    # are asserted at every method's entry and exit.
    def deposit_capped(self, amt: int) -> None:
        new_balance = self.balance + amt
        if new_balance > 1000000:
            new_balance = 1000000
        self.balance = new_balance


# ----- Method postcondition strengthening -----

class Producer:
    @icontract.ensure(lambda result: result >= 0)
    def make(self, x: int) -> int:
        return x * x


class StrictProducer(Producer):
    @icontract.ensure(lambda result: result <= 100)
    def make(self, x: int) -> int:
        # Must satisfy BOTH: result >= 0 (parent) AND
        # result <= 100 (child).
        if x > 10:
            return 100
        if x < 0:
            return 0
        return x * x


# ----- Method precondition weakening -----

class Strict:
    @icontract.require(lambda x: x > 0)
    def proc(self, x: int) -> int:
        return x + 1


class Lenient(Strict):
    # Weakens: accepts x >= 0 too.
    @icontract.require(lambda x: x >= 0)
    def proc(self, x: int) -> int:
        return x + 1


def main():
    # Invariant inheritance.
    a = CappedAccount(100)
    a.deposit_capped(2000000)
    # Capped at 1M; both invariants hold.
    assert a.balance == 1000000

    # Postcondition strengthening.
    p = StrictProducer()
    assert p.make(5) == 25
    assert p.make(20) == 100
    assert p.make(-3) == 0

    # Precondition weakening.
    le = Lenient()
    le.proc(0)  # Allowed under weakened precondition.
    le.proc(5)


main()
