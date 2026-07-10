# PLR §6.3.2: tuple * int repetition. Was unmodelled -> the tuple was returned
# UNCHANGED, so `t * 0` kept its elements (len != 0, t.index found elements) -- a
# false proof found by the mutation-oracle. Now folded precisely for a constant
# tuple (resolved via tuple_literals) and a constant count (n copies; n<=0 -> ()).
t = (1, 2)
assert t * 2 == (1, 2, 1, 2)
assert (5,) * 3 == (5, 5, 5)
assert 2 * (7,) == (7, 7)
u = t * 0
assert len(u) == 0
