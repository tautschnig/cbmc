# PLR §8.7: *args packing. Trailing positional arguments at the call
# site are packed into a tuple (modelled as a list) for the *args
# parameter.

def count(*args) -> int:
    return len(args)

assert count() == 0
assert count(1) == 1
assert count(1, 2, 3) == 3


def first_plus_rest(a: int, *args) -> int:
    return a + len(args)

assert first_plus_rest(10) == 10
assert first_plus_rest(10, 20, 30) == 12
