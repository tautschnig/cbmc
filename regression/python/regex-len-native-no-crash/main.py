import re

# Regression: under the native SMT-String backend, combining a regex call with
# len() on a (bounded) symbolic string must not crash the model parser, and
# len() of an input() string is non-negative and bounded (input() lengths are
# bounded to PYTHON_MAX_STRING_LENGTH, so the signed-64-bit length never wraps).
s = input()
m = re.search("abc", s)
assert len(s) >= 0
assert len(s) <= 64
