import re

# Soundness guard: a pattern the SMT translator cannot represent
# (here a back-reference) must degrade to a NONDET match decision, not a
# definite "no match". If the model forced no-match, `m is None` would be
# provable and this would (unsoundly) verify SUCCESSFUL. It must FAIL,
# because re.search(r"(a)\1", s) genuinely matches some inputs (e.g. "aa").
m = re.search(r"(a)\1", input())
assert m is None
