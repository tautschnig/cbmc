# --python-missing-return-check: a function with a non-None return annotation
# whose control flow can reach the implicit fall-through (no return) is flagged.
def f(x: int) -> int:
    if x > 0:
        return x
    # falls through on x <= 0 -> implicit None, but annotated -> int

f(-1)
