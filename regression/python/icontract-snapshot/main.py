# Phase 5: @icontract.snapshot + OLD.<name> binding.
#
# The snapshot decorator captures an expression at function
# entry and binds it under OLD.<name> for the postcondition
# lambda. The frontend lowers each snapshot to:
#   - A per-function symbol __icontract_old_<name>.
#   - An assignment to that symbol at function entry.
#   - Resolution of OLD.<name> in ensure lambdas via a
#     dedicated convert_attribute hook.

import icontract


_balance = 100


@icontract.snapshot(lambda: _balance, name="orig")
@icontract.ensure(lambda OLD: _balance == OLD.orig + 50)
def deposit_50() -> None:
    global _balance
    _balance = _balance + 50


def main():
    # Each call must respect _balance == OLD.orig + 50.
    deposit_50()
    assert _balance == 150
    deposit_50()
    assert _balance == 200


main()
