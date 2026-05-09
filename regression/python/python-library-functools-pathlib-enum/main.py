# Exercises the new library stubs for functools / pathlib / enum.

from functools import lru_cache, wraps, reduce, partial, cache
from pathlib import Path, PurePath, PurePosixPath
from enum import Enum, IntEnum, auto


# functools.lru_cache is an identity decorator — the decorated
# function retains its original body.
@lru_cache()
def square(x: int) -> int:
    return x * x


assert square(3) == 9
assert square(4) == 16


# functools.cache (Python 3.9+ alias) — same deal.
@cache
def cube(x: int) -> int:
    return x * x * x


assert cube(2) == 8


# functools.reduce returns a nondet value.
_ = reduce(int.__add__, [1, 2, 3], 0)


# functools.partial stores bound args on an instance.
p = partial(int.__add__, 5)
_ = p.func
_ = p.args
_ = p.keywords


# pathlib.PurePath has the attribute shape.
q = PurePath("a", "b", "c")
_ = q.name
_ = q.suffix
_ = q.stem
_ = q.parent
_ = q.parts
_ = q.is_absolute()

# pathlib.Path adds filesystem queries and I/O.
r = Path("/tmp/foo.txt")
_ = r.exists()
_ = r.is_file()
_ = r.read_text()

# enum.Enum / IntEnum — basic class usage.
class Color(Enum):
    RED = 1
    GREEN = 2
    BLUE = 3


class Code(IntEnum):
    OK = 0
    FAIL = 1


# We don't precisely model the enum class, but reference resolution
# must succeed.
_ = Color
_ = Code

assert True
