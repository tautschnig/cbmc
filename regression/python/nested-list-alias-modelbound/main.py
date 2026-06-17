# PLR object identity: a list whose elements are themselves mutable containers
# holds those elements by reference. Operations that replicate/share an element
# (repetition, concat, slice, copy, list()) alias the SAME inner object. The
# value-based model stores the inner lists by value, so it cannot propagate an
# in-place element mutation to the aliases -- which would be a FALSE PROOF.
# Such a mutation is therefore reported as a python-model-bound (assert + path
# cut), never silently proved. Read-only access stays precise.

grid = [[0, 0]] * 3
# In CPython all rows alias one inner list, so this mutation is visible through
# every row. The value model can't represent that, so it is reported+cut here.
grid[0][0] = 1
# Unreachable after the cut; present to show no false proof is produced.
assert grid[1][0] == 0
