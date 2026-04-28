x: int = 2
y: int = 0
match x:
    case 1:
        y = 10
    case 2:
        y = 20
    case _:
        y = 30
assert y == 20
