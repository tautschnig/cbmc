# Tagged union comparison: x >= y where both are untyped
def max_val(a, b):
    if a >= b:
        return a
    return b
assert max_val(3, 5) == 5
