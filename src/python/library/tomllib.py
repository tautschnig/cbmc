"""
Verification model of `tomllib` (stdlib, Python 3.11+)
and `tomli` (backport).

Both parse TOML. For verification, return an empty dict.
"""


def loads(s: str):
    return {}


def load(fp):
    return {}


class TOMLDecodeError(ValueError):
    pass
