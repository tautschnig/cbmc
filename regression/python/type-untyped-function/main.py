# Case A / Tier 3: fully untyped function — parameters need tagged unions
def process(x, y):
    if x > y:
        return x - y
    return y - x

# With --function, x and y should be nondet tagged unions
# For now, they default to int which happens to work for this test
