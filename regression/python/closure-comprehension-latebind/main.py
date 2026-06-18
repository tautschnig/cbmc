# Closure cell substrate (PLR §4.2.2), comprehension late-binding.
# In Python 3 every closure produced by a comprehension shares the loop
# variable's cell, so they all observe its FINAL value (the classic
# late-binding gotcha). The closures are dispatched out of the result
# list via the higher-order-through-container mechanism.


fns = [lambda: i for i in range(3)]
# All three closures observe the final value of i (2), not 0/1/2.
assert fns[0]() == 2
assert fns[1]() == 2
assert fns[2]() == 2

# Closures with their own parameter plus the captured (final) loop var.
adders = [lambda x: x + i for i in range(4)]
assert adders[0](10) == 13
assert adders[3](10) == 13

# Soundness: an enclosing variable of the same name is NOT clobbered by
# the comprehension's loop variable (Python-3 comprehension scope).
i = 99
gs = [lambda: i for i in range(5)]
assert i == 99

# Non-closure comprehensions are unaffected (per-element values).
squares = [n * n for n in range(4)]
assert squares[0] == 0
assert squares[2] == 4
assert squares[3] == 9
