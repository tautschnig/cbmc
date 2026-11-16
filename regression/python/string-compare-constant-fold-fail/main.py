from typing import Dict, List

LANES = 4


def route(table: Dict[str, List[str]]) -> int:
    # One row, filtered out by string compares on constant column values.
    # The compares must be decided during symbolic execution: otherwise
    # every row keeps both branches open and the drain loops below are
    # unwound on paths where nothing was queued.
    lanes = [[], [], [], []]
    for i in range(len(table['kind'])):
        if table['kind'][i] != 'MEM':
            continue
        if table['dir'][i] != 'IN':
            continue
        lanes[int(table['lane'][i])].append(table['word'][i])
    moved = 0
    while any(lanes):
        for lane in range(LANES):
            while lanes[lane]:
                lanes[lane].pop(0)
                moved += 1
    return moved


assert route({'kind': ['CPU'], 'dir': ['IN'], 'lane': ['0'],
              'word': ['w']}) == 1
