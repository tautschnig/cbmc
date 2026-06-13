def shrink(x):
    x.pop()


a = [1, 2, 3]
shrink(a)
# Soundness guard: CPython has len(a) == 2 after the by-reference pop, so this
# is FALSE and must NOT verify. Before runtime __tag-dispatch of ambiguous
# container methods, pop on an Any parameter was a nondet no-op and this
# wrongly verified SUCCESSFUL.
assert len(a) == 3
