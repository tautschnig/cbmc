"""
Verification model of the `typing` module.

CPython's typing module is mostly type hints — no runtime
semantics. We model the public surface as no-ops / identity
functions so user code that imports typing constructs doesn't
cause "unknown name" errors. The front-end sees these as
opaque stubs; verification reasons over the annotated code's
actual values, not the types.
"""


# Type variables and parameter syntax
class TypeVar:
    def __init__(self, name, *constraints, **kwargs):
        self.__name__ = name

    def __class_getitem__(cls, item):
        return cls


class ParamSpec:
    def __init__(self, name, **kwargs):
        self.__name__ = name


class TypeVarTuple:
    def __init__(self, name, **kwargs):
        self.__name__ = name


# Generic class markers
class _GenericAlias:
    def __class_getitem__(cls, item):
        return cls


class Generic(_GenericAlias):
    pass


class Protocol(_GenericAlias):
    pass


class NamedTuple(_GenericAlias):
    pass


class TypedDict(_GenericAlias):
    pass


# Annotation combinators — each returns a sentinel that the
# front-end treats as "annotation only, no runtime effect".
class _SpecialForm:
    def __class_getitem__(cls, item):
        return item

    def __getitem__(self, item):
        return item


Union = _SpecialForm()
Optional = _SpecialForm()
Callable = _SpecialForm()
Tuple = _SpecialForm()
List = _SpecialForm()
Dict = _SpecialForm()
Set = _SpecialForm()
FrozenSet = _SpecialForm()
Type = _SpecialForm()
ClassVar = _SpecialForm()
Final = _SpecialForm()
Annotated = _SpecialForm()
Literal = _SpecialForm()
TypeAlias = _SpecialForm()
TypeGuard = _SpecialForm()
Concatenate = _SpecialForm()
Unpack = _SpecialForm()
Never = _SpecialForm()
NoReturn = _SpecialForm()
Any = _SpecialForm()
Self = _SpecialForm()
LiteralString = _SpecialForm()
Required = _SpecialForm()
NotRequired = _SpecialForm()


# Abstract protocols — identity classes; our frontend picks up
# class structure for method dispatch.
class Iterable(_GenericAlias):
    pass


class Iterator(_GenericAlias):
    pass


class Generator(_GenericAlias):
    pass


class Sequence(_GenericAlias):
    pass


class MutableSequence(_GenericAlias):
    pass


class Mapping(_GenericAlias):
    pass


class MutableMapping(_GenericAlias):
    pass


class Container(_GenericAlias):
    pass


class Collection(_GenericAlias):
    pass


class Sized(_GenericAlias):
    pass


class Hashable(_GenericAlias):
    pass


class Awaitable(_GenericAlias):
    pass


class AsyncIterable(_GenericAlias):
    pass


class AsyncIterator(_GenericAlias):
    pass


class AsyncGenerator(_GenericAlias):
    pass


class ContextManager(_GenericAlias):
    pass


class AsyncContextManager(_GenericAlias):
    pass


# Decorators — identity.
def overload(func):
    return func


def final(obj):
    return obj


def no_type_check(obj):
    return obj


def runtime_checkable(cls):
    return cls


def type_check_only(obj):
    return obj


# Utility functions — return nondet / no-op at verification layer.
def cast(typ, val):
    return val


def get_type_hints(obj, *args, **kwargs):
    return {}


def get_origin(tp):
    return None


def get_args(tp):
    return ()


def assert_never(val):
    raise RuntimeError("assert_never reached")


def assert_type(val, typ):
    return val


def reveal_type(val):
    return val


def is_typeddict(tp):
    return False


# Exports
TYPE_CHECKING = False
