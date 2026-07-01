# PLR §3: dict-literal key dedup completes the whole-group beyond numeric/string.
#  - None is a singleton: `{None: 1, None: 2}` -> one key, later value.
#  - structurally-identical tuple keys are equal: `{(1,2): "a", (1,2): "b"}` ->
#    one key; distinct tuples stay separate.
# Expected: VERIFICATION SUCCESSFUL.
n = {None: 1, None: 2}
assert len(n) == 1
assert n[None] == 2

t = {(1, 2): "a", (1, 2): "b"}
assert len(t) == 1

u = {(1, 2): "a", (1, 3): "b"}
assert len(u) == 2
