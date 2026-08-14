from typing import NotRequired, TypedDict


class TD(TypedDict, total=False):
    k: NotRequired[str]


def nondet_bool() -> bool: ...


if nondet_bool():
    t = TD(k="v")
    is_error = False
elif nondet_bool():
    t = TD()
    is_error = False
else:
    t = TD()
    is_error = True

if is_error:
    e = "throttled"
else:
    e = t.get('k')
assert isinstance(e, str)   # MUST NOT VERIFY
