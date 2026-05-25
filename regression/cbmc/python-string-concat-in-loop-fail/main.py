# Companion to python-string-concat-in-loop: an INCORRECT
# assertion ("aa") must FAIL after the SSA-havoc fix.
# Before the fix this verified SUCCESSFUL because the
# constraints were UNSAT.

s = ""
for c in ["a", "b", "c"]:
    s = s + c
assert s == "aa"
