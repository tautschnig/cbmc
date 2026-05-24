# PLR §8.5: 'with EXPR as TARGET' is equivalent to:
#     manager = EXPR
#     TARGET = manager.__enter__()
#     try: BLOCK
#     finally: manager.__exit__(...)
#
# The TARGET binds the return value of __enter__, NOT the
# manager itself. Previous implementation bound the manager,
# which is wrong for the common case where __enter__ returns
# something other than self.

class IntCM:
    def __enter__(self) -> int:
        return 42
    def __exit__(self, *a):
        pass


class StrCM:
    def __enter__(self) -> str:
        return "hello"
    def __exit__(self, *a):
        pass


def int_enter() -> None:
    with IntCM() as v:
        r: int = v
    assert r == 42


def str_enter() -> None:
    with StrCM() as v:
        r = v
    assert r == "hello"


int_enter()
str_enter()
