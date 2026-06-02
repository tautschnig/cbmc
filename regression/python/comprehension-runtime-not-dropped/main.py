# differential2 §12c soundness check: previously the comprehension
# over a runtime list was dropped, so `ys` kept a prior/nondet value
# and an assertion over it could hold vacuously, masking a real bug.
# With the loop lowering the wrong assertion below is correctly caught.
def doubled(xs: list[int]) -> int:
    ys = [v * 2 for v in xs]
    return ys[0]


assert doubled([5]) == 999
