# The guard must NOT fire for fixed-eq builtin keys/elements
# (int/str/float/bool and heterogeneous python_value keys): their
# Python == IS structural/denotational.
d = {'a': 1, 'b': 2}
assert d['a'] == 1
h = {1: 'x', 'y': 2}      # heterogeneous (python_value keys)
assert h[1] == 'x'
xs = [1, 2, 3]
assert 2 in xs
assert 5 not in xs
