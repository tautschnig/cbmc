# PLR §8.7 no-false-positive guard for the *-unpack arity check. None of these
# is a provable arity violation, so none must raise:
#   - an exact-length literal unpack;
#   - a *args callee (any arity accepted);
#   - a Name-bound list unpack (length not statically known here -> conservatively
#     NOT flagged; a runtime mismatch is a legal-until-called soundness residual,
#     but flagging it would false-positive on valid calls).
# Expected: VERIFICATION SUCCESSFUL.
def f(a, b):
    return a + b


def g(*args):
    return len(args)


assert f(*[1, 2]) == 3
assert g(*[1, 2, 3, 4]) == 4
xs = [10, 20]
assert f(*xs) == 30
