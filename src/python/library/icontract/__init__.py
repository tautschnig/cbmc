"""
Verification model of the third-party `icontract` library.

icontract provides design-by-contract decorators for Python:
  @icontract.require(lambda x: x > 0)
  @icontract.ensure(lambda result: result >= 0)
  @icontract.snapshot(lambda lst: list(lst), name="orig")
  @icontract.invariant(lambda self: self.balance >= 0)

For verification, the decorators are no-ops at the Python
runtime level — they wrap the decorated callable and return it
unchanged. The CBMC Python frontend recognises them via the
decorator-list inspection in `convert_function_def` and lowers
each contract clause to the corresponding `__CPROVER_requires`
/ `__CPROVER_ensures` annotation on the GOTO function (or to
class-invariant emission on every method, for `@invariant` on
classes). See the Contracts section of `doc/python-frontend-architecture.md`.

When the frontend's icontract integration is disabled (or for
contracts that the lambda-to-contract translator can't handle),
the decorators silently pass through, mirroring the runtime
behaviour with checks switched off via `__debug__`.

Public surface:
  * require, ensure, snapshot, invariant — decorator factories.
  * DBC, DBCMeta — base class / metaclass for contract-aware
    classes.
  * ViolationError — raised on contract breach.
  * SLOW, InvariantCheckEvent — runtime hint enums.
"""


class ViolationError(AssertionError):
    """Raised when a contract is breached."""

    pass


class _ContractBase:
    """Marker base for contract-attached metadata.

    The real icontract library stores the captured lambda on the
    wrapped function object so it can be re-invoked at runtime.
    For verification we don't need to keep the metadata at the
    Python level; the frontend reads the decorator AST directly
    when emitting the DFCC annotations.
    """


def require(condition, description="", a_repr=None, error=None,
            enabled=True):
    """@require(lambda *args: pred) — precondition decorator."""

    def decorator(func):
        return func

    return decorator


def ensure(condition, description="", a_repr=None, error=None,
           enabled=True):
    """@ensure(lambda result, *args: pred) — postcondition decorator.

    Inside the lambda, the parameter named `result` resolves to
    the function's return value. The frontend lowers this to
    `__CPROVER_return_value` in the emitted ensures clause.
    """

    def decorator(func):
        return func

    return decorator


def snapshot(capture, name=""):
    """@snapshot(lambda *args: expr, name="N") — entry-state capture.

    Pairs with @ensure(lambda OLD, ...: ...OLD.N): the captured
    expression's entry-state value is bound under OLD.N inside
    the post-condition lambda. The frontend lowers this to a
    `__CPROVER_old(...)` term in the emitted ensures clause.
    """

    def decorator(func):
        return func

    return decorator


def invariant(condition, description="", a_repr=None, error=None,
              enabled=True, check_on=None):
    """@invariant(lambda self: pred) — class invariant decorator.

    Applied to a class, the invariant is asserted at the entry
    and exit of every public method (excluding __init__, where
    it is asserted only at exit since the object doesn't exist
    on entry).
    """

    def decorator(cls):
        return cls

    return decorator


class DBCMeta(type):
    """Design-by-contract metaclass.

    The real icontract library uses this metaclass to compose
    contracts across inheritance chains (Liskov rules:
    preconditions weaken, postconditions strengthen). For
    verification, the frontend handles inheritance composition
    on the contract clauses directly (see Phase 7 of the
    icontract integration plan); this class is a structural
    placeholder so `class Cls(DBC):` parses and resolves.
    """

    def __new__(mcs, name, bases, namespace, **kwargs):
        return super().__new__(mcs, name, bases, namespace)


class DBC(metaclass=DBCMeta):
    """Base class for contract-aware classes.

    Equivalent to `class Cls(metaclass=DBCMeta): ...`.
    """

    pass


# Runtime-hint enums (the real library uses an Enum; we use
# plain ints since `from enum import Enum` is heavier than
# necessary for verification stubs).
class InvariantCheckEvent:
    CALL = "CALL"
    SETATTR = "SETATTR"
    BOTH = "BOTH"


# Performance-hint marker used by the real library to skip
# expensive contracts in --slow mode. For verification we treat
# it as a no-op so SLOW-tagged contracts are still checked.
SLOW = False


# `aRepr` is the icontract repr engine. Stub as None — the
# frontend never inspects it.
aRepr = None
