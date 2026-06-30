# PLR §3.2: a dict-comprehension key must be hashable; iterating list keys ->
# TypeError ('unhashable type: list').
d = {k: 1 for k in [[1], [2]]}
