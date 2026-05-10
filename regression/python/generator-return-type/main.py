# Generator return-type inference. CPython generators return
# a list (eager-evaluation model in our frontend). The element
# type is inferred from the yield expression:
#   - yield <float>    → list[float]
#   - yield <str>      → list[str]
#   - yield <bool>     → list[bool]
#   - default          → list[int]


def ints():
    yield 1
    yield 2
    yield 3


def floats():
    yield 1.0
    yield 2.5


# int generator: list[int], sum works at int type.
total = 0
for x in ints():
    total = total + x

# float generator: sum is a float.
f_total = 0.0
for y in floats():
    f_total = f_total + y


assert total >= 0
assert f_total >= 0.0
