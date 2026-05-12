"""
Verification model of the third-party `attrs` attribute
decorator library (`attr` / `attrs`).

attrs generates __init__, __repr__, __eq__ from class
declarations. For verification, @define / @attrs / @s
decorators are no-ops and .ib() factories return their
default.
"""


def define(*args, **kwargs):
    if args and callable(args[0]):
        return args[0]

    def decorator(cls):
        return cls
    return decorator


attrs = define
s = define
dataclass = define


def field(default=None, **kwargs):
    return default


ib = field
attrib = field


def Factory(factory, takes_self: bool = False):
    if callable(factory):
        if takes_self:
            return factory
        return factory()
    return factory


def asdict(instance) -> dict:
    return {}


def astuple(instance) -> tuple:
    return ()


def evolve(instance, **changes):
    return instance


def validate(instance) -> None:
    return None


NOTHING = None
