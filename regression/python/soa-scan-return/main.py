from typing import TypedDict, List


class Item(TypedDict):
    code: int


class Resp(TypedDict):
    items: List[Item]


def get_items() -> Resp: ...


def main() -> None:
    resp = get_items()
    for it in resp['items']:
        if it['code'] == 777:
            return
    # fall-through: NO element is 777. Claiming one IS 777 must fail.
    ok = True
    for it2 in resp['items']:
        if it2['code'] == 777:
            ok = False
    assert not ok             # MUST NOT VERIFY (no element is 777 -> ok stays True)


main()
