from typing import List


def tail(words: List[str]) -> str:
    box = [[], []]
    box[0].append(words[0][2:6])
    return box[0].pop(0)


assert tail(['abcdefgh']) == 'cdef'


def drained(n: int) -> int:
    queues = [[] for _ in range(n)]
    total = 0
    while any(queues):
        for q in queues:
            while q:
                total += q.pop(0)
    return total


assert drained(3) == 0
