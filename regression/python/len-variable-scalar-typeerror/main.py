# PLR §6.10: len() of a scalar raises TypeError even when the scalar
# is held in a variable (an unguarded call, not duck-typed).
x = 7
n = len(x)
