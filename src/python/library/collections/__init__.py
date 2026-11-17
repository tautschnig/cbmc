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
    """Multiset over hashable elements (stdtypes collections.Counter).

    Modelled as a class over one typed dict per key kind -- int keys
    (`_i`), str keys (`_s`) and 2-tuples of ints (`_d`) -- because a single
    dict with untyped keys would box keys through the Any channel, which
    tuples do not round-trip. The front-end dispatches subscripts and `in`
    to the typed variant (`__getitem_int__`, `__getitem_str__`, ...) when
    the key's static type is known (keyed_dunder_candidates). Counts are
    exact: Counter(iterable) counts every occurrence, missing keys read 0
    (Counter.__missing__). Keys of other kinds are not modelled.
    """

    _d: dict[tuple[int, int], int]
    _s: dict[str, int]
    _i: dict[int, int]

    def __init__(self, iterable=None, **kwds):
        self._d = {}
        self._s = {}
        self._i = {}
        if iterable is not None:
            self._count(iterable, 1)

    def _count(self, iterable, step: int) -> None:
        # (A private name: `update` on a dict subclass would be taken for
        # dict.update by the method dispatch.)
        for x in iterable:
            if isinstance(x, int):
                self._i[x] = self._i.get(x, 0) + step
            elif isinstance(x, str):
                self._s[x] = self._s.get(x, 0) + step
            else:
                self._d[x] = self._d.get(x, 0) + step

    def update(self, iterable=None, **kwds) -> None:
        if iterable is not None:
            self._count(iterable, 1)
        return None

    def subtract(self, iterable=None, **kwds) -> None:
        if iterable is not None:
            self._count(iterable, -1)
        return None

    def __setitem__(self, k: tuple[int, int], v: int) -> None:
        self._d[k] = v

    def __setitem_str__(self, k: str, v: int) -> None:
        self._s[k] = v

    def __setitem_int__(self, k: int, v: int) -> None:
        self._i[k] = v

    def __getitem__(self, k: tuple[int, int]) -> int:
        return self._d.get(k, 0)

    def __getitem_str__(self, k: str) -> int:
        return self._s.get(k, 0)

    def __getitem_int__(self, k: int) -> int:
        return self._i.get(k, 0)

    def __contains__(self, k: tuple[int, int]) -> bool:
        return k in self._d

    def __contains_str__(self, k: str) -> bool:
        return k in self._s

    def __contains_int__(self, k: int) -> bool:
        return k in self._i

    def __len__(self) -> int:
        return len(self._i) + len(self._s) + len(self._d)

    def get(self, k: tuple[int, int], default: int = 0) -> int:
        return self._d.get(k, default)

    def keys(self):
        # One key kind (the usual case): that dict's view as a list.
        if len(self._s) == 0 and len(self._d) == 0:
            return list(self._i.keys())
        if len(self._i) == 0 and len(self._d) == 0:
            return list(self._s.keys())
        if len(self._i) == 0 and len(self._s) == 0:
            return list(self._d.keys())
        out = []
        for k in self._i:
            out.append(k)
        for k in self._s:
            out.append(k)
        for k in self._d:
            out.append(k)
        return out

    def values(self) -> list[int]:
        if len(self._s) == 0 and len(self._d) == 0:
            return list(self._i.values())
        if len(self._i) == 0 and len(self._d) == 0:
            return list(self._s.values())
        if len(self._i) == 0 and len(self._s) == 0:
            return list(self._d.values())
        out = []
        for k in self._i:
            out.append(self._i[k])
        for k in self._s:
            out.append(self._s[k])
        for k in self._d:
            out.append(self._d[k])
        return out

    def items(self):
        if len(self._s) == 0 and len(self._d) == 0:
            return list(self._i.items())
        if len(self._i) == 0 and len(self._d) == 0:
            return list(self._s.items())
        if len(self._i) == 0 and len(self._s) == 0:
            return list(self._d.items())
        out = []
        for k in self._i:
            out.append((k, self._i[k]))
        for k in self._s:
            out.append((k, self._s[k]))
        for k in self._d:
            out.append((k, self._d[k]))
        return out

    def total(self) -> int:
        t = 0
        for v in self.values():
            t = t + v
        return t

    def most_common(self, n: int = 0):
        # Ordering by count is not modelled: an unconstrained list of at
        # most n (key, count) pairs (sound for a bound, not for contents).
        out = []
        if n > 0:
            for kv in self.items():
                if len(out) < n:
                    out.append(kv)
        return out

    def elements(self):
        # Each key repeated by its count (non-positive counts omitted).
        out = []
        ks = list(self._i.keys())
        vs = list(self._i.values())
        for j in range(len(ks)):
            c = vs[j]
            while c > 0:
                out.append(ks[j])
                c = c - 1
        ks2 = list(self._s.keys())
        vs2 = list(self._s.values())
        for j in range(len(ks2)):
            c = vs2[j]
            while c > 0:
                out.append(ks2[j])
                c = c - 1
        ks3 = list(self._d.keys())
        vs3 = list(self._d.values())
        for j in range(len(ks3)):
            c = vs3[j]
            while c > 0:
                out.append(ks3[j])
                c = c - 1
        return out


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
