# Phase 3: @icontract.ensure (without `result` reference) is
# asserted at every return point.
#
# This test uses an @ensure clause that references only the
# function's parameters / state variables (not `result`), so
# Phase 3's translation suffices. Phase 4 will extend the
# mechanism to support `result`.
#
# We verify the assertion fires by deliberately inserting a
# postcondition that's true at exit thanks to a body assignment.

import icontract

# Module-level state — easy to verify without `result`.
_counter = 0


@icontract.ensure(lambda: _counter >= 0)
def bump() -> None:
    global _counter
    _counter = _counter + 1


def main():
    bump()
    bump()
    bump()
    # _counter == 3 here; the @ensure assertion was checked at
    # each return inside bump and held.
    assert _counter == 3


main()
