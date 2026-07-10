# PLR §6.2.7: a dict store with a NON-constant key (`d[b] = v`) updates the
# runtime keys/values arrays but could not update the constant dict_literals
# snapshot, which was left STALE -- so d.values()/d.keys() read the pre-insert
# snapshot (missing the new entry), letting `!=` be FALSE-PROVED (mutation-
# oracle: `d[b]=b; sorted(d.values())`). The snapshot is now dropped on a
# variable-key store, so iteration falls back to the (updated) runtime arrays.
b = 5
d = {0: 4, 1: 4}
d[b] = b
assert sorted(d.values()) == [4, 4, 5]
assert sum(d.values()) == 13
assert len(d) == 3
