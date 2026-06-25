# differential: binding a Union/Any value to an EXPLICITLY-ANNOTATED scalar
# parameter does not coerce in Python (annotations are not runtime casts). A
# str-tagged union bound to an `int`-annotated parameter and used as int raises
# TypeError. Annotation provenance (explicitly_annotated_params) lets the
# call-boundary tag obligation fire only for genuine annotations -- so an
# inferred/default scalar param (lambda / unannotated) is not flagged.
def use(y: int) -> int:
    return y - 1

def f(x: "int | str") -> int:
    return use(x)

f("s")
