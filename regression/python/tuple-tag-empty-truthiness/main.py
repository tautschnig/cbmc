# PLR §6.10.1: a boxed empty tuple () is FALSY. It used to be treated as truthy
# (boxed as CLASS -> truthy-when-present), a false proof: the else-branch CPython
# takes was skipped. The TUPLE tag models boxed-tuple truthiness soundly (nondet,
# since arity is not recoverable from the opaque box), so the else-branch is
# explored and this uncaught AssertionError is found (verification FAILS).
xs = [(), 9]
if xs[0]:
    pass
else:
    assert False
