# Python's comprehension clash rules on CONCRETE inputs (PLR 6.2.6 /
# 6.2.7, CPython-verified):
# - dict comprehension duplicate keys DEDUP: key object + insertion
#   position from the FIRST occurrence, value from the LAST
#   (build_dict_value is the one constructor implementing this;
#   the enumerated dictcomp path now routes through it -- k1);
# - set comprehension duplicates DEDUP (bitmap for small ints; the
#   bit-OR is idempotent -- k2);
# - a Name iterable bound to a tracked list literal enumerates like
#   the inline literal.
src = [1, 2, 1]
d = {k: k * 10 for k in src}
assert len(d) == 2
assert d[1] == 10

p = {k: v for k, v in [(1, 10), (2, 20), (1, 30)]}
assert len(p) == 2
assert p[1] == 30            # LAST value wins
assert p[2] == 20

s = {x for x in src}
assert len(s) == 2
assert 1 in s
assert 2 in s
assert 3 not in s
