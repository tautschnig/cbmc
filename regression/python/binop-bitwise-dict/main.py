# PLR §6.7: a bitwise operator with a concrete non-set operand (here a dict) is a
# definite TypeError. compute_binop_verdict widens the bitwise fire condition to
# unambiguous non-set operands (float/str/dict/None/complex; list is EXCLUDED as
# a non-int set literal is modelled as a list). Expected: VERIFICATION FAILED.
r = 1 | {1: 2}
