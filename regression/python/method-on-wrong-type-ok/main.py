# No false positives: a valid type-exclusive method on its OWN type, a shared
# method name, a user class defining the name, and an Any-typed receiver must
# NOT be flagged.
class HasAppend:
    def append(self, x) -> int:
        return 5


def via_any(x):
    return x.append(1)


def main() -> None:
    xs = [1, 2]
    xs.append(3)               # list.append -> valid
    assert len(xs) == 3
    s = {1, 2}
    s.add(3)                   # set.add -> valid
    assert 3 in s
    d = {"a": 1}
    d.setdefault("b", 2)       # dict.setdefault -> valid
    assert HasAppend().append(1) == 5   # user-defined method of that name
    via_any([10])              # Any receiver -> not flagged


main()
