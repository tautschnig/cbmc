from typing import NotRequired, TypedDict


class TD(TypedDict, total=False):
    k: NotRequired[str]


t = TD(k="v")
assert t["k"] == "v"          # subscript read
assert "k" in t               # membership
assert t.get("k") == "v"      # get present
b = t.get("missing")
assert b is None              # get absent -> None
assert not isinstance(b, str)
