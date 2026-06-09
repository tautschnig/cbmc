# PLR §3.2: a variable annotation is a hint and does not coerce the runtime
# value. `s: str = <int>` keeps the int value (Python allows the mismatch),
# rather than coercing through the incompatible str slot (which lost the
# value to nondet). Restricted to scalar/string mismatches.
def get_num() -> int:
    return 10


def t() -> None:
    s: str = get_num()
    assert s == 10

    n: int = "hello"  # type: ignore
    assert n == "hello"


t()
