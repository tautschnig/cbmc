# The negated form must FAIL (CPython: list(enumerate) == literal is True, so
# `!=` is False -> AssertionError). Guards against re-introducing the false proof.
xs = [7, 8]
assert list(enumerate(xs)) != [(0, 7), (1, 8)]
