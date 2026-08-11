# Regression: enumerate(seq, start) honours the start argument
# (positional or keyword). Previously the index always started at 0.

xs = [10, 20, 30]

# Default start=0
for i, x in enumerate(xs):
    assert i >= 0 and i < 3

# Positional start=5
for i, x in enumerate(xs, 5):
    assert i >= 5 and i < 8

# Keyword start=100
for i, x in enumerate(xs, start=100):
    assert i >= 100 and i < 103
