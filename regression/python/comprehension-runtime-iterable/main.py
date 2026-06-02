# differential2 §12c (comprehension over a non-literal iterable). A
# list comprehension whose iterable is a runtime list (here a parameter
# with symbolic length) used to be unsupported by the unroll path, so
# convert_list_comp returned nil and the enclosing assignment was
# silently dropped. It is now lowered to a real GOTO loop that
# populates a temporary, so the result is modeled faithfully -- element
# values, filters, and length are all observable.
def doubled(xs: list[int]) -> int:
    ys = [v * 2 for v in xs]
    return ys[0]


def filtered(xs: list[int]) -> int:
    ys = [v for v in xs if v > 2]
    return len(ys)


assert doubled([5, 6, 7]) == 10
assert filtered([1, 2, 3, 4]) == 2
