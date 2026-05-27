# Phase 6: @icontract.invariant on a class.
#
# The invariant is asserted at the entry and exit of every
# instance method (excluding __init__, which only gets the
# exit assertion since the object doesn't exist on entry).
# Static and class methods are exempt because they don't
# operate on a self instance.

import icontract


@icontract.invariant(lambda self: self.balance >= 0)
class Account:
    def __init__(self, start: int) -> None:
        self.balance = start

    def deposit(self, amt: int) -> None:
        self.balance = self.balance + amt

    def safe_withdraw(self, amt: int) -> int:
        # Guarded — preserves the invariant.
        if self.balance >= amt:
            self.balance = self.balance - amt
            return amt
        return 0


def main():
    a = Account(100)
    a.deposit(50)
    assert a.balance == 150

    taken = a.safe_withdraw(40)
    assert taken == 40
    assert a.balance == 110

    # Try to withdraw more than balance — guard prevents it.
    not_taken = a.safe_withdraw(1000)
    assert not_taken == 0
    assert a.balance == 110


main()
