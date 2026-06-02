# PLR §8.7: a keyword argument that matches no parameter (and the
# function has no **kwargs) raises TypeError.
def f(a):
    return a


f(a=1, b=2)
