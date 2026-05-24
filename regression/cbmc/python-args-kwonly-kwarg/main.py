# PLR §8.7: combined *args + keyword-only parameter + kwarg
# call. Previously the call site packed *args AFTER keyword
# handling, so kwarg-filled slots beyond the *args index were
# either swept into the *args list (if they were positional
# slots) or destroyed by the resize-then-pack reorder.
#
# Fix: pack *args BEFORE the keyword loop. The keyword loop
# then resizes to params.size() with the packed list already
# at va_idx, and writes kwarg values into the kwonly slots
# without disturbing the *args content.

def with_kwarg(a: int, *args, b: int = 10) -> int:
    return a + b + len(args)


def empty_args_with_kwarg() -> None:
    # f(1, b=20): a=1, args=[], b=20 → 1+20+0 = 21
    assert with_kwarg(1, b=20) == 21


def positionals_then_kwarg() -> None:
    # f(1, 2, 3, b=20): a=1, args=[2,3], b=20 → 1+20+2 = 23
    assert with_kwarg(1, 2, 3, b=20) == 23


def kwarg_default() -> None:
    # f(1): a=1, args=[], b=10 (default) → 1+10+0 = 11
    assert with_kwarg(1) == 11


def positionals_only() -> None:
    # f(1, 2, 3): a=1, args=[2,3], b=10 (default) → 1+10+2 = 13
    assert with_kwarg(1, 2, 3) == 13


empty_args_with_kwarg()
positionals_then_kwarg()
kwarg_default()
positionals_only()
