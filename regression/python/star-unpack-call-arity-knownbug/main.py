# KNOWNBUG (call-signature validation gap). Calling `f(*[1, 2, 3])` unpacks a
# 3-element list into a 2-parameter function -> CPython raises TypeError (takes 2
# positional arguments but 3 were given). validate_call_signature counts explicit
# positional args but does not account for a `*`-unpacked argument whose length is
# statically known (a list literal), so the arity violation is missed and the call
# is modelled as succeeding. CPython: TypeError; cbmc proves f(*[1,2,3]) == 3 (a
# false proof). Desired: VERIFICATION FAILED. Fix: fold a statically-known
# *-unpack length into the positional-arg count in the call-signature check.
def f(a, b):
    return a + b


assert f(*[1, 2, 3]) == 3
