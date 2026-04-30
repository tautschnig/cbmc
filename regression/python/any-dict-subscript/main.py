# Any-typed dict values: d["key"] where d: Dict[str, Any]
# The dict subscript should return python_value_type
from typing import Dict, Any

d: Dict[str, Any] = {"key": 42}
x: Any = d["key"]
assert True
