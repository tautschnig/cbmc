# PLR §8.7: applying a non-callable decorator (`@d` where `d = 5`) raises
# TypeError at def-time in CPython ('int object is not callable'). The frontend
# now emits a def-time TypeError for a bare `@name` decorator whose value is a
# provably non-callable concrete type. Expected: VERIFICATION FAILED.
d = 5


@d
def f() -> None:
    pass
