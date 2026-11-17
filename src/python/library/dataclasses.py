"""
Verification model of the `dataclasses` module.

Covers the decorator and helpers most commonly seen in Python
code: ``@dataclass``, ``field``, ``fields``, ``asdict``,
``astuple``, ``replace``, ``is_dataclass``, and the sentinel
``MISSING``.

The decorator is an identity wrapper at the Python layer: the
front-end preserves user-provided ``__init__``/``__repr__`` if
present and otherwise treats the annotated class as a plain
class with its declared attributes. Semantic niceties like
field-order validation, frozen enforcement, and auto-generated
``__eq__`` / ``__hash__`` are not modelled; verification reasons
over the class's fields and methods directly.
"""


class _MISSING_TYPE:
    pass


MISSING = _MISSING_TYPE()


def dataclass(cls=None, *, init=True, repr=True, eq=True, order=False,
              unsafe_hash=False, frozen=False, match_args=True,
              kw_only=False, slots=False, weakref_slot=False):
    """``@dataclass`` — identity decorator. The front-end treats
    the decorated class as a regular class."""
    def wrap(c):
        return c

    if cls is None:
        return wrap
    return cls


def field(*, default=None, default_factory=None, repr=True,
          hash=None, init=True, compare=True, metadata=None,
          kw_only=None):
    """``field(...)`` in a dataclass body is read by the front-end
    itself: ``default``, ``default_factory`` (called once per
    instance) and ``init=False``. This body is not used for that.
    (The defaults are not ``MISSING``: an instance as a parameter
    default is not modelled.)"""
    return default


def fields(class_or_instance):
    """Returns a (nondet) tuple of field descriptors. Over-
    approximation: verification code that iterates fields gets a
    nondet list."""
    return []


def asdict(instance, dict_factory=dict):
    """Returns a nondet dict of the instance's fields."""
    return {}


def astuple(instance, tuple_factory=tuple):
    """Returns a nondet tuple of the instance's field values."""
    return ()


def replace(instance, **changes):
    """Returns a nondet copy of the instance with substituted
    fields. Not a real copy — verification-layer stub."""
    return instance


def is_dataclass(obj) -> bool:
    return nondet_bool()


class FrozenInstanceError(AttributeError):
    pass


class InitVar:
    """``InitVar[T]`` marker. Identity at the verification layer."""

    def __class_getitem__(cls, item):
        return cls


class KW_ONLY:
    """Sentinel for keyword-only field boundaries."""
    pass
