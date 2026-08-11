# Reference-semantics instances (DEFAULT): class instances stored in
# containers keep per-object identity (PLR 3.1) -- iteration,
# subscript-attribute access and extract-then-mutate all reach the
# stored object; distinct elements stay distinct.
class Task:
    x: int

    def __init__(self) -> None:
        self.x = 0


# iterate-and-mutate
tasks = [Task(), Task()]
for t in tasks:
    t.x = 1
assert tasks[0].x == 1
assert tasks[1].x == 1

# subscript-attribute store
tasks[0].x = 7
assert tasks[0].x == 7
assert tasks[1].x == 1      # distinct objects: no over-aliasing

# extract-then-mutate
u = tasks[1]
u.x = 3
assert tasks[1].x == 3
assert tasks[0].x == 7

# loop-append keeps per-instance identity
gs = []
n = 0
while n < 3:
    gs.append(Task())
    n += 1
for g in gs:
    g.x = 9
assert gs[0].x == 9 and gs[2].x == 9
