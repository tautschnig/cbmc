# PLR §8.4: calling a nonexistent method raises AttributeError, which IS
# catchable -- `except Exception` catches it, the handler runs (caught[0]=1),
# and the program exits normally. cbmc now models the raise through the
# exception machinery when an enclosing handler covers it (matching CPython),
# instead of a hard assert that ignored the handler (the old lint-style
# behavior this test previously pinned). An UNCAUGHT missing method remains a
# definite VERIFICATION FAILED (see missing-method-uncaught below/other tests).
class Sensor:
    value: int

    def __init__(self) -> None:
        self.value = 0

    def read(self) -> int:
        return self.value


s = Sensor()
caught = [0]

v = s.read()
assert v == 0

try:
    s.write(42)  # no such method
except Exception:
    caught[0] = 1

assert caught[0] == 1
