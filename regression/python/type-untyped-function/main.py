# Case A / Tier 3: fully untyped function — parameters need tagged unions
def process(x, y):
    __CPROVER_assume(x >= 0 and x < 1000)
    __CPROVER_assume(y >= 0 and y < 1000)
    if x > y:
        return x - y
    return y - x
