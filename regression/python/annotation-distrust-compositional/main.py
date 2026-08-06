class R:
    rid: int
    kind: str        # lie via a NAME bound to a ternary

    def __init__(self, rid: int, kind: str) -> None:
        self.rid = rid
        self.kind = kind


fleet = []
n = 0
while n < 6:
    kind = 0 if n % 3 == 0 else 1
    fleet.append(R(n, kind))
    n += 1

skipped = 0
for res in fleet:
    if res.kind == 1:
        skipped += 1
assert skipped == 4
