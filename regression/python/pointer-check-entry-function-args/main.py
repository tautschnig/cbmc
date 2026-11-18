# --function with a list parameter and an unannotated parameter.
def first_or(xs: list[int], d) -> int:
    if len(xs) > 0:
        return xs[0]
    return 0
