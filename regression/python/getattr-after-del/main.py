# PLR §3.3.2 / §7.4: after `del self.x` (in a method), attribute access falls
# through to __getattr__ (which here returns a str). The deletable field of a
# __getattr__-class is modelled as python_value and `del` stores __getattr__'s
# result into the slot, so the cross-type use `f.x - 1` (str - int) is caught as
# a TypeError (closes c4_getattr_fallback / laurel-006). A present int field
# before the del is unaffected; a del-then-reassign restores the int.
# (Residual: a DIRECT `del f.x` in module code on an external instance is not
# detected as deletable -- only `del self.x` in the class's own methods is.)
class Fallback:
    def __init__(self) -> None:
        self.x: int = 42

    def __getattr__(self, name: str) -> str:
        return "fb"

    def clear(self) -> None:
        del self.x


def main() -> None:
    f = Fallback()
    assert f.x == 42      # present int field (unaffected)
    f.clear()             # del self.x -> slot now holds __getattr__'s "fb"
    r = f.x - 1           # "fb" - 1 -> TypeError


main()
