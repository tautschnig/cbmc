from typing import List, TypedDict


class Obj(TypedDict):
    eq_key: int
    tag: int


def get_source() -> List[Obj]: ...


def bucket_of(k: int) -> int:
    if k <= 0:
        return 0
    if k == 2:
        return 2
    return 1


source = get_source()
result = {bucket_of(it['eq_key']): it['tag'] for it in source if it['eq_key'] > 0}
for key in result:
    assert 0 <= key < 2
