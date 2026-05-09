# cbmc_c library: C string-library intrinsics accessible from
# Python. Each function is declared via @c_intrinsic; the
# front-end lowers calls to the named C function.

from cbmc_c import strlen, getenv

# strlen on a literal: CBMC's built-in model returns the exact
# length, so we can assert the value.
assert strlen("hello") == 5
assert strlen("") == 0
assert strlen("abcdef") == 6

# getenv: the call itself is pointer-safe thanks to the
# string_constantt persistence for @c_intrinsic str args.
_ = getenv("HOME")
_ = getenv("PATH")
