# PLR §3.2/§3.3: numeric scalars (int/float/bool) do not define the
# container / iterator / callable protocols, so hasattr of those
# names is precisely False.
x = 5
assert hasattr(x, "__len__") is False
assert hasattr(x, "__iter__") is False
assert hasattr(x, "__getitem__") is False
assert hasattr(x, "__call__") is False

# A duck-typing guard therefore folds: len(x) sits on the branch
# that is never taken, so the program is exception-free.
y = len(x) if hasattr(x, "__len__") else -1
assert y == -1
