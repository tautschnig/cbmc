# PLR §8.7: calling a function with more positional arguments than it
# accepts (and no *args) raises TypeError.
def f(a):
    return a


f(1, 2)
