# Negative twin: the sentinel flowing back out of d.get() IS the
# sentinel — asserting otherwise must fail (CPython: AssertionError).
# This was the exact reported false-proof shape.
_MISSING = object()


def main() -> None:
    d = {"a": 1}
    found = d.get("absent", _MISSING)
    assert found is not _MISSING


main()
