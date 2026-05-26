# PLR §6.10.1: a == b where a is a function call returning
# a python_string struct. The struct is decomposed into
# 'length' and 'data' for the string-equality solver call;
# both decompositions previously evaluated the function
# CALL independently (length came from one call, data from
# another), so the equality check used mismatched outputs
# from two non-deterministic invocations.

def make_str() -> str:
    return "abc"

def from_int(n: int) -> str:
    return "x" * n   # any deterministic string-returning function

# Struct-returning fn call on the LHS of equality.
assert make_str() == "abc"

# Same on the RHS.
assert "abc" == make_str()

# Both sides are calls (different functions).
def make_other() -> str:
    return "abc"

assert make_str() == make_other()

# Inequality.
assert make_str() != "xyz"
