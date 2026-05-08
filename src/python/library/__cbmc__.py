"""
Stub-author helpers for the CBMC Python model library.

This file is intentionally small: it declares the tiny surface that
library stubs need in order to interact with the front-end. Each
helper is a no-op at the Python level (so the stub files remain
ordinary importable Python), but the front-end recognises the
helpers' AST shape and attaches special meaning to them.

Exports:

* ``c_intrinsic(name)``
    Decorator factory. A function marked ``@c_intrinsic('foo')`` is
    lowered by the front-end to a call of the C function ``foo``
    instead of executing the Python body. The Python body is
    typically just ``...`` (ellipsis) and is never run. The C
    function must exist in the ansi-c library (CBMC will link it
    at goto-program finalisation time) and its argument and return
    types must match the Python signature.

    Example:

        from __cbmc__ import c_intrinsic

        @c_intrinsic('sin')
        def sin(x: float) -> float: ...

    Calls to ``sin(x)`` now go through the C math library's
    over-approximation of sine.
"""


def c_intrinsic(name):
    """Decorator factory — see module docstring. No runtime effect;
    the front-end consumes the decorator by AST pattern.
    """
    def _decorator(func):
        return func

    return _decorator
