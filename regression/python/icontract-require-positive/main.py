# Phase 2: precondition assumption is observable.
#
# The function body asserts x > 0. If @require lowers to a
# __CPROVER_assume at entry, the assertion holds (the assume
# constrains the verified state space). Without the assume,
# this would FAIL because x is nondet here.

import icontract


@icontract.require(lambda x: x > 0)
def assert_positive(x: int) -> int:
    # Verified only because the @require above assumed x > 0.
    assert x > 0
    return x


def main():
    # Direct call with a nondet value — no caller-side guard.
    # Phase 2's contract semantics: the body assumes the
    # precondition; the call site does not yet check it.
    # That matches the "DFCC disabled" view of contracts.
    x = nondet_int()
    assert_positive(x)


main()
