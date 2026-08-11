# PLR §8.5: collections.defaultdict / Counter — dict
# subclasses with a default factory. Missing-key reads
# return F() (the factory's zero) instead of raising
# KeyError, where F is the factory passed at construction.

from collections import defaultdict, Counter

def basic_int_factory() -> None:
    d = defaultdict(int)
    d["a"] += 1
    d["a"] += 1
    v = d["missing"]
    assert d["a"] == 2
    assert v == 0


# NOTE: str-FACTORY reads (missing-key "" and store/read-back)
# go through the refined-strings boxed scan -- an old gap that
# predates the defaultdict intrinsic (plain Dict[str, str] fails
# the same way). Pinned in
# regression/python/collections-defaultdict-knownbug.


def counter() -> None:
    # The library Counter model is TUPLE-keyed composition (the
    # documented capability); tuple-keyed counting is exact.
    c = Counter()
    c[2, 3] = 2
    assert c[2, 3] == 2
    # Missing key returns 0 (Counter.__missing__ semantics).
    assert c[7, 7] == 0
    # NOTE: string-keyed count ACCUMULATION (c["x"] += 1 thrice
    # == 3) is over-approximated by the tuple-keyed model -- moved
    # to the KNOWNBUG twin.


def aliased_import() -> None:
    # `from collections import defaultdict as dd`
    # — handled via collections_imports[asname] = name.
    # See companion test in -alias/.
    pass


def none_factory_is_plain_dict() -> None:
    # defaultdict(None) behaves like a plain dict, but explicit
    # writes still work.
    d = defaultdict(None)
    d["a"] = 42
    assert d["a"] == 42


basic_int_factory()
counter()
none_factory_is_plain_dict()
