# Soundness guard: the WRONG value must NOT be provable. A regression of the
# call-duplication bug would re-evaluate f (the 2nd eval pops again, returning
# 2) and false-prove `== 2`.
def f(l: list) -> int:
    item = l[0]
    del l[0]
    return item


h = [1, 2, 3]
assert f(h) == 2
