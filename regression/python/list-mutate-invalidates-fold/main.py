# PLR §3.3: a content-changing in-place mutation (append/insert/pop/remove/extend/
# clear) must invalidate the list constant-fold snapshot, else min()/max()/sorted()
# /index() fold against PRE-mutation data. Here `xs.append(-5)` makes min(xs) == -5,
# so `assert min(xs) == 0` must raise (it held only for the stale [4,0]).
# Found via precision triage of the value-oracle fuzzer's false alarms (the
# structural blind spot: the oracle always asserts CPython's CORRECT value, so
# a stale-fold surfaces as a false ALARM there, never a false proof). CPython:
# AssertionError; expected: VERIFICATION FAILED.
xs = [4, 0]
xs.append(-5)
assert min(xs) == 0
