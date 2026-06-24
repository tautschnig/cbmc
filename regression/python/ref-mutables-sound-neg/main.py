# Reference-semantics spike (--python-ref-mutables) soundness: a genuinely
# FALSE assertion about an aliased mutable must NOT be proved. After
# `r = g[0]; r.append(5)` the shared object is [1, 5], so `len(g[0]) == 1` is
# false and must produce VERIFICATION FAILED (no false proof). This guards
# against the reference wrapping silently dropping the mutation.
g = [[1]]
r = g[0]
r.append(5)
assert len(g[0]) == 1
