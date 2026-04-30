# PLR §4.12.5: Any is compatible with every type
# Any-typed variables should work in arithmetic, comparison, subscript
from typing import Any

x: Any = 42
y: int = x + 1
assert y == 43

s: Any = "hello"
t: str = s
