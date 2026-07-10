# PLR §3.3: a subscript store `xs[i] = v` (and `xs[i] += v`) mutates the list in
# place and must invalidate its constant-fold snapshot -- else min/max/sorted/
# index fold pre-mutation. `xs[0] = 9` makes min(xs) == 2, so `min(xs) == 1`
# (the stale value) must raise. CPython: AssertionError; expected: FAILED.
xs = [1, 2]
xs[0] = 9
assert min(xs) == 1
