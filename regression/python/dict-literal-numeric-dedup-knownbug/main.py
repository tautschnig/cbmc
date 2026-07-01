# KNOWNBUG (container-literal cross-type numeric dedup cluster). A dict LITERAL
# counts distinct keys with type-sensitive equality, so `{True: 1, 1: 2}` is
# modelled with two keys. Python uses numeric equality for dict keys: True == 1
# (and both hash equal), so `{True: 1, 1: 2}` has ONE key (value 2, the later
# store wins). CPython: len == 1, so this `assert len({True: 1, 1: 2}) == 2`
# raises AssertionError; cbmc proves len == 2 (false proof). Desired: VERIFICATION
# FAILED. NB the dict-SUBSCRIPT store path already dedups (`d = {1: "a"};
# d[True] = "b"` -> len 1), as does `dict([(1, "a"), (1.0, "b")])` -- only the
# literal builder is affected. Fix: dedup dict-literal keys by numeric equality.
assert len({True: 1, 1: 2}) == 2
