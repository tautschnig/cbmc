def make_adder(n: int):
    return lambda x: x + n

add5 = make_adder(5)
assert add5(3) == 8
