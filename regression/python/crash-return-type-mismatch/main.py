# Return type mismatch: function declares list[dict] but returns list[value]
from typing import List, Dict, Any

class Service:
    client: object
    def get_items(self) -> List[Dict[str, Any]]:
        response: object = self.client
        items = response["Items"]
        return items

s = Service()
result = s.get_items()
assert len(result) >= 0
