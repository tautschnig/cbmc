class R:
    rid: int

    def __init__(self, rid: int) -> None:
        self.rid = rid


fleet = []
n = 0
while n < 4:
    fleet.append(R(n))
    n += 1

assert fleet[0].rid == 3
assert fleet[1].rid == 3
assert fleet[2].rid == 3
assert fleet[3].rid == 3
