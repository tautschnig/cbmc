from math import sqrt

# sqrt returns nondet float (no precise model), but the function
# is recognized and callable (no "no body" warning)
x: float = sqrt(4.0)
# We can't assert x == 2.0 without a sqrt model, but we can verify
# that the program doesn't crash and the function is resolved
