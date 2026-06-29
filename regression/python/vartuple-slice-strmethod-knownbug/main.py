# KNOWNBUG (false proof, ty-010): slicing a variable-length tuple then calling a
# str method. `tuple[int, ...]` is modelled as python_value (Any), so `t[1:]` is
# Any and the `-> str` return annotation is trusted, so `f(...).upper()` is
# accepted -- but the runtime value is a tuple with no .upper(). CPython raises
# AttributeError. The CONCRETE-receiver variant (e.g. (1,2,3).upper()) IS caught
# (str-method-on-non-str); this Any-erasure variant needs a real
# variable-length-tuple model. Desired: VERIFICATION FAILED.
def f(t: "tuple[int, ...]") -> str:
    return t[1:]


f((1, 2, 3)).upper()
