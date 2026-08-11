def make_dict() -> dict[str, list[str]]:
    return {
        "foo": ["bar", "baz"],
        "qux": ["quux", "quuz"]
    }

d = make_dict()
l = d["foo"]
assert len(l) == 2
for s in l:
    assert s == "bar" or s == "baz"
