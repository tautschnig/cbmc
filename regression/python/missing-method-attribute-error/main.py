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
