# PLR §8.5: 'with EXPR as v' is equivalent to:
#   manager = EXPR
#   v = manager.__enter__()
#
# The bound name receives __enter__'s return value, regardless
# of whether EXPR is a constructor call (already handled in
# convert_with) or any other expression that produces a class
# instance (factory function, variable holding a CM, etc.).
# Previously the non-constructor form just bound v = EXPR,
# missing __enter__'s return.

class CM:
    def __enter__(self) -> int:
        return 42
    def __exit__(self, *a):
        pass


def factory_function() -> None:
    def make_cm():
        return CM()
    with make_cm() as v:
        r: int = v
    assert r == 42


def variable_with() -> None:
    cm = CM()
    with cm as v:
        r: int = v
    assert r == 42


factory_function()
variable_with()
