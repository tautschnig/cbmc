import re

# Soundness guard: outside the precise subset, re.sub must be a NONDET
# over-approximation, never a definite (possibly wrong) value. Here the
# pattern "a+" is variable-length and CPython re.sub is greedy
# (leftmost-longest) while SMT-LIB str.replace_re_all is leftmost-shortest,
# so they diverge. The model must therefore NOT commit to a specific result;
# this exact-value assertion must be falsifiable (VERIFICATION FAILED).
r = re.sub("a+", "X", input())
assert r == "X"
