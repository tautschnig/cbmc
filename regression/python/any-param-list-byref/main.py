def mutate(x):
    # x is unannotated -> Any / python_value parameter. The list is shared
    # by reference, so this element mutation must propagate to the caller.
    x[0] = 99


a = [1, 2]
mutate(a)
assert a[0] == 99
