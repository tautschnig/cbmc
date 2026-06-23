"""
Verification model of `tomllib` (stdlib, Python 3.11+)
and `tomli` (backport).

Both parse TOML. For verification, return an empty dict.
"""


def loads(s: str) -> dict:
    return nondet_dict(8)


def load(fp) -> dict:
    return nondet_dict(8)


class TOMLDecodeError(ValueError):
    pass
