# No-false-positive guard for the int-attribute AttributeError check:
# real int methods on a tracked scalar must NOT be flagged, and an
# UNTRACKED int-typed parameter (a stub return the frontend defaulted to
# int) must not spuriously fault (sound direction: untracked receivers
# are not flagged).
def real_methods() -> None:
    x = 5
    assert x.bit_length() == 3
    assert x.bit_count() == 2


def untracked(n: int) -> int:
    return n.bit_length()


real_methods()
assert untracked(4) >= 0
