# --python-check-annotations (P3): widening and same-kind numeric conversions
# are compatible (no false positive). int/bool ARE assignable where a float is
# declared (numeric tower); bool is assignable where int is declared.
def takes_float(x: float) -> None:
    pass

def takes_int(n: int) -> None:
    pass

takes_float(3)      # int -> float : widening, OK
takes_float(True)   # bool -> float : OK
takes_int(True)     # bool -> int : OK (bool <: int)
takes_int(5)        # int -> int : OK
