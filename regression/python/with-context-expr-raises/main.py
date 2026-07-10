# PLR §8.5: if the CONTEXT EXPRESSION of a `with` raises while being evaluated
# (`with CM(t.index(9)) as v:` -- t.index raises ValueError), the exception
# propagates and the body never runs. convert_with never flushed the pending
# exception checks from evaluating the context expression, so the ValueError was
# silently dropped and the body/rest ran unguarded -- a false proof found by the
# mutation-oracle. The checks are now flushed before the body.
class CM:
    def __init__(self, v):
        self.v = v
    def __enter__(self):
        return self.v
    def __exit__(self, *a):
        return False
t = (5, 8, 5)
with CM(t.index(9)) as v:
    a = v
