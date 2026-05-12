from typing import Dict


def count(d: Dict[str, int]) -> int:
    return len(d)


result = count({"a": 1, "b": 2, "c": 3})
assert result == 3


def lookup(d: Dict[str, str], key: str) -> str:
    return d[key]


r = lookup({"env": "prod", "version": "1.2.3"}, "env")
assert r == "prod"
