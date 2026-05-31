# KNOWNBUG: --python-unbounded-ints --z3 with `**` in an icontract
# precondition lambda triggers an SMT2 backend invariant violation
# (`Unknown typecast integer -> float`) at smt2_conv.cpp:3615 in
# convert_typecast.
#
# Repro from the user-facing problem report on the icontract phase
# of the Python frontend. The frontend correctly lowers
# `@icontract.require(lambda x: x != -(2**31))` to a __CPROVER_assume
# at function entry, but the constant-fold path for `2**31` inside
# the lambda emits an integer-typed value that gets typecast to
# float somewhere on the way into the bit-vector model used by the
# rest of the function. The Z3 SMT2 conversion has no rule for
# integer→float and aborts with an invariant failure.
#
# In default int64 mode the same source is processed correctly and
# produces a counterexample for the orthogonal int64 overflow at
# x = -(2**63) (the user's precondition is stated in terms of
# INT32_MIN, which doesn't constrain int64).
#
# When this is fixed, this test should report VERIFICATION
# SUCCESSFUL (the precondition combined with unbounded-int
# semantics rules out the abs() overflow case entirely).

import icontract


@icontract.require(lambda x: x != -(2**31))
@icontract.ensure(lambda result: result >= 0)
def abs_val(x: int) -> int:
    if x < 0:
        return -x
    return x
