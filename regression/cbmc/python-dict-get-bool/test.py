# .get on a bool-typed dict must not crash the frontend.
# Regression for arith_tools.cpp:149 from_integer precondition violation
# when the dict value type cannot hold the int "None" sentinel.

d = {"a": False}
x = d.get("a")
assert x is False

e = {"a": True}
y = e.get("a")
assert y is True

# .get with a default also exercises the safe_typecast path.
z = d.get("missing", True)
assert z is True
