# cbmc_c library: both 64-bit-int string-library functions and
# 32-bit-int ctype / toupper / tolower / abs functions.

from cbmc_c import strlen, getenv
from cbmc_c import isdigit, isalpha, isspace, toupper, tolower, abs

# 64-bit: strlen folds under CBMC's model.
assert strlen("hello") == 5
assert strlen("") == 0

# getenv: pointer-safe via the string_constantt persistence.
_ = getenv("HOME")

# 32-bit ctype predicates
assert isdigit(ord("5")) != 0
assert isdigit(ord("a")) == 0

assert isalpha(ord("x")) != 0
assert isalpha(ord("1")) == 0

assert isspace(ord(" ")) != 0
assert isspace(ord("x")) == 0

# 32-bit transforms
assert toupper(ord("a")) == ord("A")
assert toupper(ord("A")) == ord("A")  # already upper
assert tolower(ord("Z")) == ord("z")
assert tolower(ord("z")) == ord("z")  # already lower

# abs on a constant int
assert abs(-5) == 5
assert abs(7) == 7
