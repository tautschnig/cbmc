# PLR §8.5: the `with EXPR as v` variable is reassigned to __enter__'s result, so
# its tracking must be invalidated. `b = 1` tracks b=1; `with CM(3) as b` rebinds
# b to 3, but a later `t[b]` folded on the stale b=1. Found by proactive probing.
class CM:
    def __init__(self, v):
        self.v = v
    def __enter__(self):
        return self.v
    def __exit__(self, *a):
        return False
b = 1
t = (6, 3, 9)
with CM(3) as b:
    pass
r = t[b]
