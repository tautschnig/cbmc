# --python-strict must enable the FULL strictness family, including the two
# checks that are not part of the original annotation-only set:
# --python-check-iter-none (here) and --python-check-any-arg-attrs. Iterating
# None is a TypeError; the preset alone (no individual check flag) must catch it.
x = None
for i in x:
    pass
