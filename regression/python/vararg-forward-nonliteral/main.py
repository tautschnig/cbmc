# PLR §8.7: `h(*xs)` forwarding a NON-literal list into a *args parameter. The
# vararg absorbs a symbolic number of elements; the fixed-count spread read only
# `params - already` (one slot) elements, so sum(args) computed sum(xs[:1]) not
# sum(xs) -- a false proof (found by the mutation-oracle). The vararg is now a
# sound nondet for a non-literal spread. sum([1,2,3]) is 6, so `!= 6` raises.
def h(*args):
    return sum(args)
xs = [1, 2]
xs.append(3)
r = h(*xs)
assert r != 6
