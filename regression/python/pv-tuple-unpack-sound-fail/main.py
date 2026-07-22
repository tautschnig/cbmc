# Sound floor for unpacking a python_value (Any / a TUPLE-tagged field
# like `t.shape`) whose concrete arity we cannot resolve: the targets
# get a sound nondet binding rather than being DROPPED. Previously the
# whole `M, N = t.shape` + the assert vanished -> a vacuity false proof
# (ESBMC github_4515_attr_fail). Now the false assert is a real FAIL.
class T:
    def __init__(self, d0: int, d1: int):
        self.shape: tuple = (d0, d1)


t: T = T(10, 20)
M, N = t.shape
assert M == 99
