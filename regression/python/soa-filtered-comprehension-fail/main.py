from typing import TypedDict, List


class Item(TypedDict):
    code: int


def get_items() -> List[Item]: ...


xs = get_items()
codes = [it['code'] for it in xs if it['code'] > 500]
# COMPLETENESS is not asserted: counting facts must stay unprovable.
assert len(codes) == 0        # MUST NOT VERIFY (some element may pass)
