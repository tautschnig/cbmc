# PLR §3.2: Any return type loses concrete value
from typing import Any
def foo(x: float) -> Any:
    if x > 0:
        return x + 1
    return x - 1
assert foo(2.0) == 3.0
