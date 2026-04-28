# PLR §7.11: import inside function body
def compute() -> int:
    import math
    return math.floor(3.7)
assert compute() == 3
