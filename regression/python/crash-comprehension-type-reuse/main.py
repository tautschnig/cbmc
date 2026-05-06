# Variable 'x' used as dict in first comprehension, string in second
from typing import Any

class Checker:
    tags: list[str]
    def __init__(self):
        self.tags = ["Name", "Env"]
    
    def check(self, items: list[dict]) -> list[str]:
        # x is a dict element here
        mapping: dict[Any, Any] = {x["Key"]: x["Value"] for x in items}
        # x is a string here (same variable name, different type)
        missing: list[str] = [x for x in self.tags if x not in mapping]
        return missing

c = Checker()
result = c.check([{"Key": "Name", "Value": "test"}])
assert "Env" in result
