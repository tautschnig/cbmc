# PLR §6.2.6 / §7.2: a dict-comprehension tuple target destructures
# each element. Previously the converter read the target's "id" (absent
# on a Tuple node), mis-binding downstream (an invariant abort).
def f():
    d = {k: v for k, v in [(1, 2), (3, 4)]}
    assert d[1] == 2
    assert d[3] == 4
    assert len(d) == 2


f()
