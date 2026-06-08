# A function value passed to a user-defined higher-order function and
# called through the parameter must dispatch to the actual callable, via
# per-call-site monomorphisation, instead of becoming a nondet "no body
# for callee". A distinct clone per (call site, callable) keeps it sound
# when the same HOF is called with different callables.
def apply(fn, x):
    return fn(x)


def inc(x):
    return x + 1


def t() -> None:
    # lambda argument, arithmetic
    assert apply(lambda a: a + 1, 5) == 6
    # named function argument
    assert apply(inc, 10) == 11
    # same HOF, a different lambda -> distinct sound clone
    assert apply(lambda a: a * 3, 4) == 12
    # nested higher-order application
    assert apply(lambda a: apply(inc, a), 7) == 8


t()
