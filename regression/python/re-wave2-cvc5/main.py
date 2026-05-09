# Wave 2 re: end-to-end demonstration of SMT regex on --cvc5.
#
# Under the default (refine-strings) solver the intrinsic degrades
# to a sound nondet — so the assertions below can't be proven and
# the test would report VERIFICATION FAILED. Under --cvc5 the
# interception in smt2_conv emits str.in_re with the translated
# regex, the subject is a string literal we can substitute into
# the SMT term, and the solver reduces at preprocessing time.
#
# This test exercises the *direct* intrinsic __cbmc_re_match; the
# library wrapper re.match still needs the subject-at-SMT-time
# integration (the 'a-prime' refactor) to propagate literal args
# through the library body.

# Positive match — anchored at start.
m = __cbmc_re_match("abc", "abcxyz")
assert m

# Negative match — pattern appears inside but not at start.
m2 = __cbmc_re_match("xyz", "abcxyz")
assert not m2

# fullmatch: whole string must match.
m3 = __cbmc_re_fullmatch("abc", "abc")
assert m3

m4 = __cbmc_re_fullmatch("abc", "abcd")
assert not m4

# search: match anywhere.
m5 = __cbmc_re_search("xyz", "abcxyz")
assert m5
