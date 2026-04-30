def has_item(lst, item):
    return item in lst

assert has_item([1, 2, 3], 2)
assert not has_item([1, 2, 3], 5)
