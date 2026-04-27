# PLR §2.4.5: map(func, iterable) — modeled as nondet list
def double(x: int) -> int:
    return x * 2
result = list(map(double, [1, 2, 3]))
# Can't assert content (nondet), but no crash
