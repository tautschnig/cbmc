# Distinct instances are distinct objects: mutating one must NOT affect another.
# Soundness guard for the reference-semantics work -- it must preserve identity
# for true aliases WITHOUT over-aliasing unrelated instances.
class V:
    def __init__(self) -> None:
        self.x: int = 1


a = V()
b = V()
b.x = 99
assert a.x == 1
assert b.x == 99
