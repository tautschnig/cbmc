# KNOWNBUG (false proof, c4/laurel-006): after `del self.x`, attribute access
# falls through to __getattr__ (which returns a different type). The field is
# concretely int-typed and cannot hold __getattr__'s str return, so the frontend
# does not model the retype -- `del` currently havocs the slot to nondet (sound
# for a stale-value read, but it does not produce the str). CPython: f.x after
# the del returns "fb" (str) and `f.x - 1` raises TypeError. Modelling this needs
# per-instance field-presence tracking or python_value fields. Desired:
# VERIFICATION FAILED.
class Fallback:
    def __init__(self) -> None:
        self.x: int = 42

    def __getattr__(self, name: str) -> str:
        return "fb"


def main() -> None:
    f = Fallback()
    del f.x
    r = f.x - 1   # __getattr__ -> "fb"; "fb" - 1 raises TypeError in CPython


main()
