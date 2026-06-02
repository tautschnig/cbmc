# PLR §6.10: next() on an exhausted (here, empty) iterator raises
# StopIteration.
it = iter([])
x = next(it)
