"""Regression for one-hop aliasing in --python-check-any-arg-attrs.

Same pattern as `any-erasure-cross-function/` but the `frobnicate`
call goes through a one-hop alias (`tmp = client`) within the
`process_alias` function. The Any-erasure walker's first pass
recognises `tmp = client` as an alias and the second pass folds
`tmp.frobnicate(7)` into attributes of the canonical parameter
`client`.

Expected: the same attribute-error as the direct case, anchored
at the call site `process_alias(manager)`.
"""


class Manager:
    state: int

    def __init__(self) -> None:
        self.state = 0

    def step(self) -> int:
        self.state += 1
        return self.state


def process_alias(client: object) -> int:
    tmp = client
    return tmp.frobnicate(7)


def main() -> int:
    manager: Manager = Manager()
    return process_alias(manager)


main()
