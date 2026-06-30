# @dataclass construction must bind fields at ALL construction sites, including
# `return Config(...)` and `with Res(...) as r` (a dataclass that is also a
# context manager). Routed through the unified build_class_construction helper.
from dataclasses import dataclass


@dataclass
class Config:
    host: str
    port: int


def make() -> Config:
    return Config("h", 8080)


def main() -> None:
    c = make()
    assert c.port == 8080


main()
