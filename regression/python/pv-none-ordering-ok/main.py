# No-false-positive guard: a python_value that is never None on any path
# (all branches return int) must NOT be flagged when ordered against a
# number; and a caught TypeError composes.
def always_int(x: int) -> int:
    if x < 0:
        return 1
    return 2


def maybe_none(x: int) -> int:
    if x < 0:
        return -x
    return None


assert always_int(5) < 10  # never None -> no spurious TypeError

r = maybe_none(5)
try:
    _ = r < 0
    caught = False
except TypeError:
    caught = True
assert caught
