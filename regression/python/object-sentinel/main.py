# PLR §3.1: object() creates a new object with its own identity; the
# sentinel pattern compares it with `is`/`is not`. Previously object()
# fell to the unknown-call path: the module-level assignment was
# dropped, every read became an unresolved name, and the asserts were
# silently dropped (false proofs); a function-local x = object() bound
# a nondet int that could collide with the None sentinel (false alarm
# on `x is not None`).
_MISS = object()
_OTHER = object()


def main():
    d = {"a": 1}
    hit = d.get("a", _MISS)
    assert hit is not _MISS
    miss = d.get("absent", _MISS)
    assert miss is _MISS
    assert _MISS is not _OTHER
    assert _MISS is not None
    assert bool(_MISS)


def local_sentinel():
    x = object()
    assert x is not None


main()
local_sentinel()
