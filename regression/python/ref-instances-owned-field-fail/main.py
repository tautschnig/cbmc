# Negative twin: two holders' owned fields are DISTINCT -- claiming
# identity must be REFUTED (guards against the shared-__ctor_temp
# over-aliasing false proof this feature replaced).
class Inner:
    def __init__(self, v: int) -> None:
        self.v = v


class Holder:
    def __init__(self) -> None:
        self.x = Inner(1)


h1 = Holder()
h2 = Holder()
assert h1.x is h2.x
