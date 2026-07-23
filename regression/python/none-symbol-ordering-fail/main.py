# PLR §6.10.1: ordering (< > <= >=) between None and a number is a
# TypeError. A SYMBOL bound to None carries the pv-NONE struct only in
# its ASSIGN (its symbol value is empty), so a use site sees just the
# symbol -- `x = None; x < 0` proved vacuously (a false proof). The
# none_constants set (populated at the note_mutable_extraction chokepoint,
# cleared on any rebind) now categorises it as None so the comparison
# faults. Inline `None < 0` was already caught.
def f() -> None:
    x = None
    assert (x < 0) == False


f()
