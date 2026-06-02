# PLR §3.2/§3.3: built-in containers expose the sequence/mapping
# protocols; hasattr must resolve them precisely (True), and reject
# protocols the type lacks (False).
s = "abc"
assert hasattr(s, "__len__") is True
assert hasattr(s, "__getitem__") is True
assert hasattr(s, "__call__") is False

xs = [1, 2, 3]
assert hasattr(xs, "__iter__") is True
assert hasattr(xs, "__setitem__") is True

d = {"a": 1}
assert hasattr(d, "keys") is True
assert hasattr(d, "__contains__") is True

t = (1, 2)
assert hasattr(t, "__len__") is True
assert hasattr(t, "__setitem__") is False
