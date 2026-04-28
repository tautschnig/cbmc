class Box:
    def __init__(self) -> None:
        self.value: int = 0

def mutate(b: Box) -> None:
    b.value = 42

box: Box = Box()
mutate(box)
assert box.value == 42
