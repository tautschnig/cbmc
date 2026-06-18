# re.search with a non-string pattern raises TypeError. Under
# --python-raising-ops-check the may-raise is modeled (the Any-typed
# pattern is not provably a string).
import re
from typing import Any


def f() -> None:
    pat: Any = 123
    try:
        re.search(pat, "abc")
    except AssertionError:
        assert False


f()
