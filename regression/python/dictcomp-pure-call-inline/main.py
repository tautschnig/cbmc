from typing import List, TypedDict


class Obj(TypedDict):
    eq_key: int
    tag: int


TAG_LIMIT = 1000
NUM_BUCKETS = 3


def get_source() -> List[Obj]: ...


def bucket_of(k: int) -> int:
    """doc."""
    if k <= 0:
        return 0
    if k == 2:
        return 2
    return 1


def value_of(tag: int) -> int:
    if tag < 0:
        return 0
    if tag >= TAG_LIMIT:
        return 0
    return tag


source = get_source()

source = get_source()
result = {bucket_of(it['eq_key']): value_of(it['tag']) for it in source if it['eq_key'] > 0}
for key in result:
    assert 0 <= key < NUM_BUCKETS
    assert 0 <= result[key] < TAG_LIMIT
