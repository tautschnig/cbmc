"""Regression for --python-check-any-arg-attrs (default-on for .py source).

Cross-function Any-erasure: `process(client)` calls
`client.frobnicate(...)` on the parameter, but `Manager` has no
`frobnicate` method. The static type of `client` inside `process`
is `Any`, so the missing-method check on the receiver type alone
cannot fire — we need the caller-side body sniff.

Expected: attribute-error fires at the call site
`process(manager)` referencing the missing `frobnicate` method
on class `Manager`, anchored at the caller's source line.
"""


class Manager:
    state: int

    def __init__(self) -> None:
        self.state = 0

    def step(self) -> int:
        self.state += 1
        return self.state


def process(client: object) -> int:
    # `frobnicate` does not exist on Manager.
    return client.frobnicate(7)


def main() -> int:
    manager: Manager = Manager()
    return process(manager)


main()
