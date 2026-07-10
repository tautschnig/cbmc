# PLR §6.3.2: tuple + tuple concatenation. Previously fell through to the
# arithmetic path (a plus_exprt on tuple structs), which was unmodelled. Now
# folded precisely when both operands are constant tuples (mixed element types
# included).
assert (1, 2) + (3,) == (1, 2, 3)
assert (1, 2) + (3,) != (1, 2, 4)
assert (1, "a") + (2,) == (1, "a", 2)
assert () + (5,) == (5,)
