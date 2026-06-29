# PLR §3.3.2 / §7.4: a DIRECT `del f.x` in module code (on an external instance,
# not `del self.x` in a method) of a __getattr__-class. The deletable-field scan
# is module-wide (keys on the attribute name), so `x` is typed python_value and
# the Delete handler -- which resolves f's class at the del site -- stores
# __getattr__'s result into the slot. So `f.x - 1` ("fb" - 1) is caught.
class F:
    def __init__(self) -> None:
        self.x: int = 42

    def __getattr__(self, name: str) -> str:
        return "fb"


f = F()
del f.x          # external del; slot now holds __getattr__'s "fb"
r = f.x - 1      # "fb" - 1 -> TypeError
