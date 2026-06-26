# Precision: a function annotated `-> int` with one return path that genuinely
# yields a union/Any value (return x where x: int|str) used to pun that
# python_value into the concrete int return slot, corrupting the value read on
# the OTHER (taken) path -- g("abc") mis-evaluated. The slot now widens to
# python_value when a return path genuinely yields one (gated on
# saw_python_value_return, so mutually-recursive `-> bool` functions, whose
# inferred type is python_value only out of forward-ref uncertainty, are NOT
# widened). Soundness preserved: a returned value keeps its tag (misuse still
# raises -- see union-use-after-mutation / the operator tag obligation).
def g(x: "int | str") -> int:
    if isinstance(x, str):
        return len(x)
    return x


assert g("abc") == 3
assert g(5) == 5
