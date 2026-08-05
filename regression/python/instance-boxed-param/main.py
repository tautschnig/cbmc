class R:
    rid: int

    def __init__(self, rid: int) -> None:
        self.rid = rid


def probe(res: R) -> int:
    return res.rid


fleet = []
n = 0
while n < 4:
    fleet.append(R(n))
    n += 1

s = 0
for res in fleet:
    s += probe(res)
assert s == 6


# mutation through the by-ref param must propagate (PLR 3.1)
def bump(res: R) -> None:
    res.rid = res.rid + 10


bump(fleet[0])
assert fleet[0].rid == 10
