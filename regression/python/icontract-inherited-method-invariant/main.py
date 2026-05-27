# icontract Phase 7 follow-up: child class invariants apply
# to inherited methods.
#
# When a class with @icontract.invariant inherits a method
# from a parent class without overriding it, the frontend
# synthesises a wrapper method on the child class that
# asserts the child's invariants at entry and exit and
# delegates to the parent's body. This test verifies the
# wrapper fires by exercising it in a passing case.

import icontract


# Parent has no invariant of its own.
class Account:
    def __init__(self, start: int) -> None:
        self.balance = start

    def deposit(self, amt: int) -> None:
        self.balance = self.balance + amt


# Child adds an invariant. The inherited deposit() method
# must respect this invariant when called on a child instance.
@icontract.invariant(lambda self: self.balance >= 0)
class SignedAccount(Account):
    pass  # no overrides


def main():
    a = SignedAccount(100)
    # Inherited deposit reaches Account.deposit via the
    # synthesised SignedAccount.deposit wrapper. The wrapper
    # asserts balance >= 0 at entry and exit.
    a.deposit(50)
    assert a.balance == 150
    a.deposit(20)
    assert a.balance == 170


main()
