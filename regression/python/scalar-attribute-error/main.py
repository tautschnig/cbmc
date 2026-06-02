# PLR §6.10: a numeric scalar has no arbitrary attribute, so accessing
# one raises AttributeError. Restricted to a constant numeric receiver
# (the only case provably a genuine scalar rather than a non-scalar
# object the frontend defaulted to int).
y = (5).foo
