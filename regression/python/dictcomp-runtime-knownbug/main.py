# differential2 §12c residual: dict comprehension over a runtime
# iterable. PLR: {x: x*2 for x in [1,2,3]} has 3 entries. The unroll
# path only enumerates literal lists / constant range(); over a runtime
# list parameter the dict comprehension is a sound nondet
# over-approximation (so len(d) is nondet and the assertion cannot be
# established). A precise loop lowering needs a find-or-insert dict
# store with duplicate-key dedup in the loop body.
def f(xs: list) -> int:
    d = {x: x * 2 for x in xs}
    return len(d)


assert f([1, 2, 3]) == 3
