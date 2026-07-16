# CORE [soundness]: this string op inside a TWICE-CALLED function must not
# poison the formula (global emitter symbols + two executions -> conflicting
# constraints -> UNSAT -> assert False below vacuously SUCCESSFUL). Same class
# as string-concat-twice-called-function; found by the emitter-scope audit.
def f(n: int):
    return str(n)


f(1)
f(2)
assert False