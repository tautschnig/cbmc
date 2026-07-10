# PLR §6.2.4: a comprehension whose ELEMENT contains a nested comprehension
# (`[len([j for j in range(i)]) for i in range(3)]`) was not re-evaluated per
# outer iteration -- the inner comp's outer-var-dependent iterable produced
# definite-WRONG element values, a false proof found by the mutation-oracle. Such
# a comprehension is now a sound nondet list. CPython value is [0,1,2], so the
# negated `!= [0,1,2]` raises (verification FAILS -- no false SUCCESS).
r = [len([j for j in range(i)]) for i in range(3)]
assert r != [0, 1, 2]
