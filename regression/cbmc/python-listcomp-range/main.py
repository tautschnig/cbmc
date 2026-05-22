# PLR §6.2.5: list comprehension with range() iterable
xs = [x*x for x in range(4)]
assert xs == [0, 1, 4, 9]

# In a function body — was previously dropped silently
def f():
    ys = [x + 10 for x in range(3)]
    assert ys == [10, 11, 12]
f()

# range(start, stop)
zs = [i for i in range(2, 5)]
assert zs == [2, 3, 4]
