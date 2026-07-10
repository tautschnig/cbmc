# PLR §6.10: index() on an EMPTY tuple always raises ValueError. The symbolic-arg
# tuple.index path was guarded on a non-empty tuple, so an empty tuple (e.g. from
# a slice `(a, len(s))[2:3]`) with a NON-constant argument silently succeeded --
# a false proof found by the mutation-oracle. An empty tuple now raises for any
# argument.
s = "bab"
a = 1
xs = [3, 7]
t = (a, len(s))[2:3]
r = t.index((a % (len(xs) + 1)))
