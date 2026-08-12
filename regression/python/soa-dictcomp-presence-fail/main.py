from typing import TypedDict, List


class Item(TypedDict):
    ident: int


def get_items() -> List[Item]: ...


xs = get_items()
by = {it['ident']: it['ident'] * 2 for it in xs if it['ident'] > 500}
# Vacuity control: a plainly false fact MUST still refute (assumes SAT).
assert len(by) < 0
