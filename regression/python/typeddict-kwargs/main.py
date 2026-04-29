from typing import TypedDict, Unpack

class Config(TypedDict):
    host: str
    port: int

def connect(**kwargs: Unpack[Config]) -> int:
    return kwargs["port"]

result: int = connect(host="localhost", port=8080)
assert result == 8080
