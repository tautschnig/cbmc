# PLR §4.12.5: Any type should accept any value
# When Any-typed variables are passed to str-typed parameters,
# the tagged union should be unwrapped to string.
from typing import Any

class Service:
    name: str = "svc"
    def process(self, data: str) -> str:
        return data

s: Service = Service()
result: Any = s.process("hello")
output: str = s.process(result)
assert True
