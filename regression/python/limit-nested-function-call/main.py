# PLR §6.3.4: nested calls
def double(x: int) -> int:
    return x * 2
def inc(x: int) -> int:
    return x + 1
assert inc(double(3)) == 7
