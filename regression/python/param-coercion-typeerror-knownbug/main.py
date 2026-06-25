# differential: passing a Union/Any value to a concretely-typed scalar parameter
# does NOT coerce in Python (annotations are not runtime casts). Here a str-tagged
# union value is bound to an `int` parameter and used as int; CPython raises
# TypeError. cbmc coerces via unwrap_value (reads __int_val, no tag check), so it
# verifies. A tag obligation at the call boundary is BLOCKED: parameter types are
# not reliable annotations (lambdas / unannotated params default to a scalar type
# and truly accept Any), so a blanket obligation false-alarms (e.g. passing an
# object to a lambda param inferred as int). Needs reliable real-annotation info.
def use(y: int) -> int:
    return y - 1

def f(x: "int | str") -> int:
    return use(x)

f("s")
