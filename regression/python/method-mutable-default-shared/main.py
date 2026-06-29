# PLR §8.7: a mutable default argument is evaluated ONCE (at def time) and SHARED
# across all calls -- including across different instances. A method`s mutable
# default was previously copied fresh per call, so the accumulation was lost
# (a10_mutable_default). It is now frozen into a static-lifetime symbol with a
# once-only module-init, so appends persist and accumulate across calls.
class C:
    def add(self, items: list = []) -> int:
        items.append(1)
        return len(items)


def main() -> None:
    c = C()
    assert c.add() == 1
    assert c.add() == 2     # SAME shared list -> accumulates
    d = C()
    assert d.add() == 3     # shared across instances too (Python semantics)


main()
