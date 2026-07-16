"""Model of the `sys` module for verification.

`argv` is a nondet list of strings -- `sys.argv[i]` is subscriptable and may
IndexError exactly when the program does not guard the length, matching
CPython. `exit` raises SystemExit per PLR.
"""
from typing import Any

argv: list = nondet_list(4, nondet_str())

version: str = "3.12.0"
platform: str = "linux"
maxsize: int = 9223372036854775807


def exit(code: Any = 0) -> None:
    # PLR: sys.exit raises SystemExit (not an Exception subclass; a bare
    # `except Exception` does not catch it).
    raise SystemExit(code)
