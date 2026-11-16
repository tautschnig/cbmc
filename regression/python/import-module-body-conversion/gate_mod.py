import dataclasses

LANES = 8
MARK = 0x1800


@dataclasses.dataclass
class Item:
    kind: int
    tag: int


def unpack(text: str) -> Item:
    v = int(text, 16)
    return Item(kind=v & 0xff, tag=v >> 8)


def is_mark(tag: int) -> bool:
    return tag & 0xffff == MARK >> 8


def check(table: dict) -> None:
    lanes = [[] for i in range(LANES)]
    for i in range(len(table['w'])):
        if table['src'][i] != 'MEM':
            continue
        lanes[int(table['lane'][i])].append(table['w'][i][2:2 + 16])
    spare = 0
    armed = [False for i in range(LANES)]
    credit = [spare for i in range(LANES)]
    while any(lanes):
        for k in range(LANES):
            while lanes[k]:
                it = unpack(lanes[k].pop(0))
                if it.kind == 0:
                    if is_mark(it.tag):
                        if not (armed[k] or credit[k] > 0):
                            raise AssertionError()
                        armed[k] = False
                elif it.kind == 1:
                    armed[k] = True
