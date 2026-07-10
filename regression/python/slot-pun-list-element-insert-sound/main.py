# CORE (2026-07-10): the insert(index, value) path is now covered by the
# element-inference widening (the value is the 2nd arg). A mismatched insert into
# list[int] preserves the tag, so isinstance(xs[0], int) correctly FAILS.
def src():
    return "x"


xs: list[int] = []
xs.insert(0, src())
assert isinstance(xs[0], int)
