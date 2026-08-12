from typing import TypedDict, List


class Item(TypedDict):
    ident: int


def get_items() -> List[Item]: ...


xs = get_items()
by = {it['ident']: it['ident'] * 2 for it in xs if it['ident'] > 500}
for k in by:
    assert k > 501     # MUST NOT VERIFY (k == 501 possible)
