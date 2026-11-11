# An operand of and/or is evaluated once, also when it has side effects:
# (p or True) with a nondet p is always truthy.
s = nondet_str()
if s == "Caf\u00e9":
    assert s.lower().islower() or True
x = nondet_int()
y = (nondet_int() > 3) or (x < 0) or True
assert y
