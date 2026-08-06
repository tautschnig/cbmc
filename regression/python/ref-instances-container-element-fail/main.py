# twin: a genuinely false post-mutation assert still FAILs
class Task:
    x: int

    def __init__(self) -> None:
        self.x = 0


tasks = [Task(), Task()]
for t in tasks:
    t.x = 1
assert tasks[0].x == 2
