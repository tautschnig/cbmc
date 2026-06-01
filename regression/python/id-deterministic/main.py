# Built-in id(): deterministic identity for addressable objects.
# id(x) == id(x) for the same object; aliases (y = x) share an id;
# distinct objects have distinct ids. Previously id() was unmodeled
# (nondet int), so even id(x) == id(x) could be false.
x = [1, 2, 3]
assert id(x) == id(x)
y = x
assert id(y) == id(x)
z = [1, 2, 3]
assert id(z) != id(x)
