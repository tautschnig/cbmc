"""
Verification model of the `enum` module.

Supports the four public base classes ``Enum`` / ``IntEnum`` /
``Flag`` / ``IntFlag`` plus the ``auto()`` helper, ``unique`` and
``verify`` decorators, and the ``StrEnum`` / ``ReprEnum`` Python
3.11+ additions.

``Enum`` subclasses are modelled as plain classes with ``.name`` and
``.value`` attributes; enumeration of members at runtime is not
precisely tracked. Practically this means ``for m in MyEnum`` gives
an opaque iteration result and ``MyEnum.X`` returns an instance
with nondet attributes.
"""


class Enum:
    """Base class for user-defined enumerations."""

    name: str
    value: int

    def __init__(self, value=None):
        self.name = ""
        self.value = 0 if value is None else value

    def __str__(self) -> str:
        return ""

    def __repr__(self) -> str:
        return ""

    @classmethod
    def _missing_(cls, value):
        return None


class IntEnum(int, Enum):
    """Enumeration where members are also int subclasses."""


class Flag(Enum):
    """Support for bit-flag enumerations."""


class IntFlag(int, Flag):
    """Integer-backed bit-flag enumeration."""


class StrEnum(str, Enum):
    """Python 3.11+: string-backed enumeration."""


class ReprEnum(Enum):
    """Python 3.11+: mixin that preserves the mixed-in type's
    ``__repr__``."""


# Flag-control constants that various enum decorators accept.
CONFORM = 0
CONTINUOUS = 1
STRICT = 2
EJECT = 3
KEEP = 4


class auto:
    """Placeholder for automatically-assigned enum values."""

    value: int

    def __init__(self):
        self.value = 0


def unique(enumeration):
    """Class decorator that ensures members have unique values.
    Verification-level: identity."""
    return enumeration


def verify(*checks):
    """Class decorator used with flag-control constants; identity
    at verification level."""

    def decorator(enumeration):
        return enumeration

    return decorator


def property(func):
    """Decorator used on Enum methods to make them accessible as
    attributes on the class. Identity."""
    return func


def member(value):
    """Mark a value as a member of an enum."""
    return value


def nonmember(value):
    """Mark a value as *not* a member of an enum."""
    return value


def global_enum(cls):
    """Class decorator that makes an enum's members available as
    top-level module names. Identity."""
    return cls


def pickle_by_global_name(enum_member, proto):
    return None


def pickle_by_enum_name(enum_member, proto):
    return None
