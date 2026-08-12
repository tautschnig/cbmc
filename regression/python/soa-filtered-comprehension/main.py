from typing import TypedDict, List


class Item(TypedDict):
    code: int


class Resp(TypedDict):
    items: List[Item]


def get_items() -> Resp: ...


def main() -> None:
    resp = get_items()
    codes = [it['code'] + 1 for it in resp['items'] if it['code'] > 500]
    for it in resp['items']:
        if it['code'] == 777:
            return
    for c in codes:
        assert c != 778


main()
