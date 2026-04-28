# PLR §7.11: import inside function — variable not in scope
def compute() -> float:
    import math
    return math.cos(0.0)
assert compute() == 1.0
