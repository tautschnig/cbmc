# Native SMT-String backend (--python-smt-strings): regex via the
# __cbmc_re_* intrinsics is precise for a constant pattern over both
# constant and SYMBOLIC subjects (str.in_re), including character classes.
s = nondet_string(6)
assume(s == "abcxyz")

# Constant + symbolic subject, match / search / fullmatch.
assert __cbmc_re_match("abc", "abcxyz")
assert not __cbmc_re_match("xyz", "abcxyz")
assert __cbmc_re_search("bcx", s)
assert not __cbmc_re_search("zzz", s)
assert __cbmc_re_fullmatch("abcxyz", s)

# Character class over a symbolic subject.
d = nondet_string(3)
assume(d == "123")
assert __cbmc_re_fullmatch("[0-9]+", d)

# Non-vacuity: a proven match constrains the symbolic subject's content,
# so a contradictory content claim is refuted (this assert must hold).
assert not __cbmc_re_fullmatch("[0-9]+", "12a")
