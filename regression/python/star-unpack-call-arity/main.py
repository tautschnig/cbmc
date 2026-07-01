# PLR §8.7: `f(*[1, 2, 3])` unpacks a 3-element list into a 2-parameter function
# -> CPython TypeError (takes 2 positional arguments but 3 were given).
# validate_call_signature now folds a statically-known *-unpack length (a
# list/tuple literal) into the positional count, so the arity violation is
# caught. CPython: TypeError; expected: VERIFICATION FAILED.
def f(a, b):
    return a + b


assert f(*[1, 2, 3]) == 3
