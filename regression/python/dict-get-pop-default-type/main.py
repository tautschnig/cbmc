# PLR §6.4.6: d.get(k, default) and d.pop(k, default) return d[k] if k is present
# else `default` -- so the result type is value_type | type(default). An empty
# dict (key provably absent) returns the default in ITS OWN type, and a present
# constant key returns the value. Previously the default was COERCED to the
# dict's value-element type ("s" -> int), losing its type and false-proving a
# later use (ty-005 Any-laundering).
def main() -> None:
    e = {}
    assert e.get("k", "s") == "s"    # absent -> default str (not coerced to int)
    assert e.pop("k", "s") == "s"    # absent -> default str

    p = {"k": 5}
    assert p.get("k", "s") == 5      # present -> value (int)
    assert p.pop("k", "s") == 5      # present -> value (int)

    q = {"a": 7}
    assert q.get("a", 0) == 7        # matching-type default unaffected
    assert q.get("z", 0) == 0


main()
