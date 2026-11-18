# String operations on a symbolic str with an SMT backend.
s = nondet_string(3)
assert not s.startswith("ab")
