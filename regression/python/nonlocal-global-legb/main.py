# PLR 7.13 (nonlocal) / 7.12 (global) / 4 (execution model).
# Nonlocal resolves to the nearest enclosing function's
# scope, not the module scope or a new local.


# 1. Basic nonlocal: inner reads and writes enclosing x.
def with_nonlocal() -> int:
    x = 10
    def inner() -> int:
        nonlocal x
        x = x + 5
        return x
    return inner()


assert with_nonlocal() == 15


# 2. Global write from nested function.
g = 0


def set_g() -> None:
    global g
    g = 42


set_g()
assert g == 42


# 3. Multi-level nonlocal: grandchild reaches grandparent's local.
def grand() -> int:
    y = 100
    def parent_fn() -> int:
        def child() -> int:
            nonlocal y
            y = y + 1
            return y
        return child()
    return parent_fn()


assert grand() == 101


# 4. Read-only enclosing access (no nonlocal keyword needed).
def reader() -> int:
    z = 7
    def peek() -> int:
        return z
    return peek()


assert reader() == 7
