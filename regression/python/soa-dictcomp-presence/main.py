from typing import TypedDict, List


class Item(TypedDict):
    ident: int


def get_items() -> List[Item]: ...


xs = get_items()
by = {it['ident']: it['ident'] * 2 for it in xs if it['ident'] > 500}
if len(xs) > 0 and xs[0]['ident'] == 777:
    assert 777 in by            # D5: a passing element's key IS present
