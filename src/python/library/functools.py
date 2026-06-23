"""
Verification model of the `functools` module.

Covers the commonly-used decorators and higher-order functions:
``lru_cache``, ``cache``, ``wraps``, ``update_wrapper``, ``partial``,
``partialmethod``, ``reduce``, ``cmp_to_key``, ``total_ordering``.

Decorators are implemented as identity wrappers (no caching /
wrapping semantics enforced at the verification level); they
preserve the decorated function's body and signature. ``reduce``
returns a nondet value of the initial / element type.
"""


def lru_cache(maxsize=128, typed=False):
    """@lru_cache[(...)] — identity decorator. Caching is a
    memory/perf concern, not a semantic one, so we ignore it."""
    # Support both @lru_cache and @lru_cache()
    if callable(maxsize) and typed is False:
        return maxsize

    def decorator(func):
        return func

    return decorator


def cache(func):
    """@cache — Python 3.9+ alias for lru_cache(maxsize=None)."""
    return func


def wraps(wrapped, assigned=None, updated=None):
    """@wraps(f) — identity decorator. Attribute propagation is
    cosmetic at runtime; verification-irrelevant."""

    def decorator(wrapper):
        return wrapper

    return decorator


def update_wrapper(wrapper, wrapped, assigned=None, updated=None):
    return wrapper


class partial:
    """``functools.partial(f, *a, **kw)`` — stores a function and
    some pre-applied arguments. Instances are callable. We model
    the state as three attributes; calling the instance returns a
    nondet value."""

    func = None
    args = ()
    keywords = None

    def __init__(self, func, *args, **keywords):
        self.func = func
        self.args = args
        self.keywords = keywords

    def __call__(self, *args, **keywords):
        return None


class partialmethod:
    """``functools.partialmethod`` — like ``partial`` but intended
    as a descriptor on classes."""

    func = None
    args = ()
    keywords = None

    def __init__(self, func, *args, **keywords):
        self.func = func
        self.args = args
        self.keywords = keywords


def reduce(function, iterable, initializer=None):
    """``functools.reduce(f, [a, b, c], i)`` — apply f
    cumulatively. Returns a nondet value of the iterable's
    element type; we don't enumerate the iterable at
    verification time."""
    # Value-dependent result -> sound nondet (was None : false proof for
    # `functools.reduce(...) is None`; CPython returns the accumulated value).
    return nondet_int()


def cmp_to_key(mycmp):
    """Wrap a two-argument comparator as a key function. The real
    implementation returns a class instance; for verification we
    just return a function-shaped stand-in."""

    def key(obj):
        return 0

    return key


def total_ordering(cls):
    """Class decorator that fills in missing ordering methods.
    Identity at verification level."""
    return cls


def singledispatch(func):
    """@singledispatch — identity decorator. We do not model the
    dispatch; callers see the decorated function's body."""
    return func


def singledispatchmethod(func):
    """Method-form of singledispatch."""
    return func
