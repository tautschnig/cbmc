# PLR fold-soundness rule (fold_covers_call_shape): a conversion-time
# fold of a builtin/method call may only fire when it models EVERY
# argument the call passes; unconsumed arguments force the sound
# fallback. Each stale assert below folded to a PROOF before the rule
# (ESBMC-suite adversarial tests, CPython-confirmed false proofs), and
# each true assert pins the modeled semantics where cheap (sum start,
# variadic set ops). The min/max nil TAIL is also pinned: an unmatched
# shape must produce a nondet value, never a nil that silently DROPS
# the enclosing statement.
assert sum([1, 2, 3], 10) == 16
assert sum([1, 2, 3]) == 6

xs = [3, 1, 2]
xs.sort(reverse=True)
assert len(xs) == 3  # havoc keeps the length

assert [1, 2, 1, 2].index(2) == 1

a = {1}
u = a.union({2}, {3})
assert 3 in u
assert 1 in u and 2 in u
d = {1, 2, 3}.difference({1}, {3})
assert 2 in d
assert 1 not in d and 3 not in d
