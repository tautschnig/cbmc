# PLR §6.2.2: print() arguments are evaluated. With
# --overflow-check enabled, expressions inside print() args
# (e.g. a + 1) must trigger overflow checks like any other
# arithmetic.

a = nondet_int()
print(a + 1)
