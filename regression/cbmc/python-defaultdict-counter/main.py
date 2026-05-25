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


def str_factory() -> None:
    d = defaultdict(str)
    d["name"] = "hello"
    assert d["name"] == "hello"


def counter() -> None:
    c = Counter()
    c["x"] += 1
    c["x"] += 1
    c["x"] += 1
    assert c["x"] == 3
    # Missing key returns 0
    assert c["nope"] == 0


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
str_factory()
counter()
none_factory_is_plain_dict()
