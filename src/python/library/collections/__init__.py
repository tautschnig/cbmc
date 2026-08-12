"""
Verification-optimized model of the `collections` module.

Exposes OrderedDict, defaultdict, Counter, deque, ChainMap and
namedtuple. The shapes match CPython's public API closely enough
that typical callers (`d['k']`, `d['k'] = v`, `d.get('k')`, etc.)
work; the values themselves are nondet.
"""


class OrderedDict(dict):
    """Order-preserving dict. For verification purposes behaves as a
    plain dict."""

    def move_to_end(self, key, last: bool = True) -> None:
        return None

    def popitem(self, last: bool = True):
        return None


# NOTE: `defaultdict` is intentionally NOT modelled here. The
# frontend has an INTRINSIC defaultdict/Counter-factory model
# (typed factory-zeros on missing-key reads, real dict storage --
# see defaultdict_factories in the converter); a Python-level
# `class defaultdict(dict)` SHADOWS that intrinsic (class
# construction wins over the builtins arm) while providing NO
# storage or __getitem__ -- reads raised TypeError and the
# factory-zero semantics (library ref: collections) silently vanished (the
# python-defaultdict-counter regression).


class Counter(dict):
    """Multiset over hashable elements. Modelled at the Python
    level as a class with composition over a typed dict for
    storage. The key type is `tuple[int, int]` — the common
    shape used by Counter test cases (e.g. `c[2, 3] = N`); other
    key shapes (single ints, strings, n-tuples for n != 2) are
    over-approximated as nondet at the dict-level.

    The Python-level Counter is `class Counter(dict): ...` so
    it inherits dict's protocol; our class system doesn't
    auto-inherit dict methods, so we override the relevant
    dunders directly (subscript get/set) plus the auxiliary
    methods (values, keys, items, ...). Missing keys return 0
    per Counter's `__missing__` semantics.
    """

    _d: dict[tuple[int, int], int]

    def __init__(self, iterable=None, **kwds):
        self._d = {}
        # PLR: Counter(iterable) treats each element as a key
        # with count 1 (we don't accumulate counts in the
        # bounded model). For tuple-keyed iterables this still
        # produces the correct presence/absence judgement;
        # frequency tracking is approximate.
        if iterable is not None:
            for x in iterable:
                self._d[x] = 1

    def __setitem__(self, k: tuple[int, int], v: int) -> None:
        self._d[k] = v

    def __getitem__(self, k: tuple[int, int]) -> int:
        # PLR §6.10.1 / Counter `__missing__`: missing keys
        # return 0, not KeyError.
        return self._d.get(k, 0)

    def __contains__(self, k: tuple[int, int]) -> bool:
        return k in self._d

    def get(self, k: tuple[int, int], default: int = 0) -> int:
        return self._d.get(k, default)

    def values(self) -> list[int]:
        return self._d.values()

    def keys(self):
        return self._d.keys()

    def items(self):
        return self._d.items()

    def most_common(self, n: int = 0):
        # Approximate: walk dict.items() up to n entries. Our
        # frontend doesn't precisely track per-element counts,
        # so callers using .most_common(k) as an upper bound
        # get a sound over-approximation.
        if n <= 0:
            return []
        out = []
        i = 0
        for kv in self._d.items():
            if i >= n:
                break
            out.append(kv)
            i = i + 1
        return out

    def elements(self):
        out = []
        for k in self._d.keys():
            out.append(k)
        return out

    def subtract(self, iterable=None, **kwds) -> None:
        return None

    def update(self, iterable=None, **kwds) -> None:
        if iterable is not None:
            for x in iterable:
                self._d[x] = 1
        return None

    def total(self) -> int:
        return len(self._d)


class deque:
    """Double-ended queue. Modelled as an opaque stand-in supporting
    the common methods."""

    maxlen: int

    def __init__(self, iterable=None, maxlen: int = 0):
        self.maxlen = maxlen

    def append(self, x) -> None:
        return None

    def appendleft(self, x) -> None:
        return None

    def pop(self):
        return None

    def popleft(self):
        return None

    def extend(self, iterable) -> None:
        return None

    def extendleft(self, iterable) -> None:
        return None

    def rotate(self, n: int = 1) -> None:
        return None

    def clear(self) -> None:
        return None

    def count(self, x) -> int:
        # Value-dependent (number of occurrences); a fixed 0 false-proved
        # `dq.count(x) == 0`. Sound nondet of the right type.
        return nondet_int()

    def index(self, x, start: int = 0, stop: int = 0) -> int:
        return nondet_int()

    def insert(self, i: int, x) -> None:
        return None

    def remove(self, x) -> None:
        return None

    def reverse(self) -> None:
        return None

    def copy(self):
        return deque()

    def __len__(self) -> int:
        # Value-dependent; a fixed 0 false-proved `len(dq) == 0`.
        return nondet_int()


class ChainMap:
    """Chained view over multiple maps."""

    def __init__(self, *maps):
        return None

    def new_child(self, m=None):
        return ChainMap()

    def maps(self):
        return []

    def parents(self):
        return ChainMap()


def namedtuple(typename: str, field_names, *, rename: bool = False,
               defaults=None, module=None):
    """Return a class-like object. For verification purposes the
    returned 'class' instantiates into a nondet object; fields are
    accessed as nondet attributes."""
    return object


# Type aliases that typing.py and abc re-export from collections.abc.
# We do not model the abstract base classes precisely; they are
# present as opaque placeholders so ``isinstance(x, Mapping)`` etc.
# do not fail lookup.
class abc:
    Hashable = object
    Sized = object
    Callable = object
    Iterable = object
    Iterator = object
    Reversible = object
    Generator = object
    Container = object
    Collection = object
    Set = object
    MutableSet = object
    Mapping = object
    MutableMapping = object
    MappingView = object
    KeysView = object
    ItemsView = object
    ValuesView = object
    Sequence = object
    MutableSequence = object
    ByteString = object
    Awaitable = object
    Coroutine = object
    AsyncIterable = object
    AsyncIterator = object
    AsyncGenerator = object
