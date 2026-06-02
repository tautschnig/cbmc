# differential2 §12b residual: cross-branch UnboundLocalError. x is
# assigned only on the c=True path, so f(False) raises UnboundLocalError
# in CPython. The straight-line detection sees x's local symbol (created
# while converting the if-branch) and does not flag the read. Catching
# this path-sensitively needs a runtime is-bound flag per local set at
# every binding site; a flag set only at the common assignment path
# produced false positives across constructor calls, comprehension
# results, etc. (no single assignment chokepoint exists), so it is left
# as a documented residual rather than risk unsound false alarms.
def f(c: bool) -> int:
    if c:
        x = 1
    return x


f(False)
