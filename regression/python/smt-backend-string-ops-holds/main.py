# String operations on a symbolic str with an SMT backend.
s = nondet_string(3)
__CPROVER_assume(s.startswith("ab"))
assert len(s) == 3 and s[0] == "a"
