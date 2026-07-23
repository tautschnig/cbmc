# PLR §6.10.1: ordering a number against None raises TypeError. A
# python_value operand may carry the NONE tag at runtime -- e.g. a value
# returned via a function fall-through / `return None` into a widened
# int|None slot. The pv-numeric ordering path read the operand's
# __int_val (the None SENTINEL) and compared numerically -- a false
# proof. A runtime obligation now raises TypeError when the pv tag is
# NONE (catchable, and false when the value is not None).
def f(x: int) -> int:
    if x < 0:
        return -x
    return None


r = f(5)
assert (r < 0) == False
