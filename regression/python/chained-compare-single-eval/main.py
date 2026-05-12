# PLR §6.10.1: In chained comparisons like x < b < z, the
# middle operand b is evaluated exactly once. If the
# frontend re-evaluates side-effectful expressions, this
# counter would read 2 rather than 1.

counter = [0]


def bump() -> int:
    counter[0] = counter[0] + 1
    return 5


x = 1
z = 10
r = x < bump() < z
assert r
assert counter[0] == 1
