# differential2 §12b cross-branch UnboundLocalError. x is assigned only
# on the c=True path, so f(False) raises UnboundLocalError in CPython.
# A runtime is-bound flag per plain-Assign local (false at entry, set
# true after the binding statement, asserted at each read) makes this
# path-sensitive, so the c=False path is detected even though x's local
# symbol exists from converting the if-branch.
def f(c: bool) -> int:
    if c:
        x = 1
    return x


f(False)
