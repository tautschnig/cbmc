# PLR §3.2: built-in scalars and containers are not callable, so
# callable() resolves to False for them.
x = 5
assert callable(x) is False
assert callable(3.14) is False
assert callable([1, 2]) is False
assert callable("abc") is False
assert callable({1: 2}) is False
