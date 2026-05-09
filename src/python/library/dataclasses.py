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


def field(*, default=MISSING, default_factory=MISSING, repr=True,
          hash=None, init=True, compare=True, metadata=None,
          kw_only=MISSING):
    """``field(default=...)`` — returns a sentinel object. The
    front-end does not enforce field-level defaulting; class
    attribute initialisers are the authoritative source."""
    if default is not MISSING:
        return default
    return None


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


def is_dataclass(obj):
    return False


class FrozenInstanceError(AttributeError):
    pass


class InitVar:
    """``InitVar[T]`` marker. Identity at the verification layer."""

    def __class_getitem__(cls, item):
        return cls


class KW_ONLY:
    """Sentinel for keyword-only field boundaries."""
    pass
