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


class defaultdict(dict):
    """Dict with a default-factory fallback. The real CPython
    implementation calls ``default_factory()`` on a missing key; for
    verification we model it as: reads of missing keys return a
    nondet value of the factory's return type (or nondet int when
    the factory is unknown)."""

    default_factory = None

    def __init__(self, default_factory=None, *args, **kwargs):
        self.default_factory = default_factory

    def __missing__(self, key):
        return None


class Counter(dict):
    """Multiset. ``Counter(iterable).get(x, 0)`` returns a nondet
    non-negative int."""

    def __init__(self, iterable=None, **kwds):
        return None

    def most_common(self, n: int = 0):
        return []

    def elements(self):
        return []

    def subtract(self, iterable=None, **kwds) -> None:
        return None

    def update(self, iterable=None, **kwds) -> None:
        return None


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
        return 0

    def index(self, x, start: int = 0, stop: int = 0) -> int:
        return 0

    def insert(self, i: int, x) -> None:
        return None

    def remove(self, x) -> None:
        return None

    def reverse(self) -> None:
        return None

    def copy(self):
        return deque()

    def __len__(self) -> int:
        return 0


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
