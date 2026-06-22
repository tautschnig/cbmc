# Plan A native SMT-String backend: nondet_string(N) must constrain the
# string length to EXACTLY N (matching the refined backend and the
# documented semantics), not merely bound it to [0, MAX]. Previously the
# native path ignored the size argument, so `s` could be "" and a
# length-dependent assertion spuriously FAILED.
s = nondet_string(5)
assert len(s) == 5

t = nondet_string(0)
assert len(t) == 0
