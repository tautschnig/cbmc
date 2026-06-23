# PLR: a pure subclass of int (no own attributes) behaves as int.
class uint256(int):
    pass


MOD = 123


def foo(x: uint256) -> int:
    return x % MOD


assert foo(uint256(5)) == 5
assert uint256(5) % 123 == 5
assert uint256(5) + 1 == 6
