def grow(x):
    x.extend([7, 8])


a = [1, 2]
grow(a)
assert len(a) == 4
