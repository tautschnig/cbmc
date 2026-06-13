def grow(x):
    # x is unannotated -> Any / python_value parameter. The list is shared
    # by reference, so a length-changing method must propagate to the caller.
    x.append(99)


a = [1, 2]
grow(a)
assert len(a) == 3
assert a[2] == 99
