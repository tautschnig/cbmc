# differential2 §12a (closures capture by cell, not by value). In
# CPython a closure over a loop variable captures the variable's cell,
# not its value at creation time (late binding), so every lambda made
# in the loop shares the final value: fns[0]() == fns[1]() == 2.
# CBMC captures the value at lambda creation, so fns[0]() == 0 and the
# assertion fails. Faithfully modeling this needs shared mutable cells
# for captured free variables rather than the current value capture
# (passed as extra call arguments).
fns = [lambda: i for i in range(3)]
assert fns[0]() == 2
