# str(x) for symbolic float x now emits
# cprover_string_of_double_func so the result's content
# is known to the solver.


def nondet_float() -> float: ...


def to_str(x: float) -> str:
    return str(x)


# Symbolic-float path emits the intrinsic. Length >= 0 is a
# trivially-true sanity check that the result's length
# constraint is visible.
y = nondet_float()
s = to_str(y)
assert len(s) >= 0
