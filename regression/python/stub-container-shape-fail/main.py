# twin: the length is well-formed but otherwise UNKNOWN
from typing import List


def get_names() -> List[str]: ...


ns = get_names()
assert len(ns) == 3
