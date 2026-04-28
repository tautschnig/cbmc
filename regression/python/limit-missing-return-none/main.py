# PLR §7.6: missing return should return None, not nondet
def maybe(x: int) -> int:
    if x > 0:
        return x
    # Falls through — should return None

result = maybe(-1)
assert result is None  # Should pass but fails (returns nondet)
