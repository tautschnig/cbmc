# PEP 484 numeric tower: int (and bool) promote to float. An INT-tagged
# python_value (union/Any) bound to a float slot must read its integer payload
# promoted to float, not the unset __float_val. Previously f(10) read the float
# slot (0.0/garbage); now it correctly yields 10.0.
def use(y: float) -> float:
    return y

def f(x: "int | float") -> float:
    return use(x)

assert f(10) == 10.0
assert f(2.5) == 2.5
