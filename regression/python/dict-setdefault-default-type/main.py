# PLR §6.4.6: setdefault(k, default) inserts AND returns `default` when k is
# absent, so the dict's value type must accommodate type(default). For an empty
# dict the value type is now inferred from the setdefault default, so
# {}.setdefault("k", "s") returns AND stores the str "s" consistently (both the
# returned value and a later d[k] read are str). Previously the default was
# coerced to the dict's int value type, losing the str -- the setdefault analogue
# of the get/pop (ty-005) default-type laundering.
def main() -> None:
    d = {}
    v = d.setdefault("k", "s")
    assert v == "s"          # returned default, str (not coerced to int)
    assert d["k"] == "s"     # stored value is the same str (consistent)

    e = {}
    assert e.setdefault("n", 5) == 5   # matching int default unaffected
    assert e["n"] == 5


main()
