# Phase 1: icontract library stub is recognised and decorators
# are no-ops at the Python level. The frontend's contract
# lowering arrives in Phase 2; this test only verifies that
# importing icontract and applying its decorators does not
# break verification.

import icontract


@icontract.require(lambda x: x > 0)
@icontract.ensure(lambda result: result >= 0)
def double(x: int) -> int:
    return x + x


@icontract.invariant(lambda self: self.value >= 0)
class Counter:
    def __init__(self, start: int) -> None:
        self.value = start

    def inc(self) -> None:
        self.value = self.value + 1


def main():
    # Decorators do nothing at runtime in the stub; the call
    # behaves like an undecorated function.
    assert double(3) == 6
    c = Counter(5)
    c.inc()
    assert c.value == 6


main()
