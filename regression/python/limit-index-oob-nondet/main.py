# Nondet list access may trigger bounds check
lst = [1, 2, 3]
i: int = nondet_int()
assume(i >= 0)
assume(i < 3)
assert lst[i] >= 1
