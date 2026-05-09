"""
Stub-author helpers for the CBMC Python model library.

This file is intentionally small: it declares the tiny surface that
library stubs need in order to interact with the front-end. Each
helper is a no-op at the Python level (so the stub files remain
ordinary importable Python), but the front-end recognises the
helpers' AST shape and attaches special meaning to them.

Exports:

* ``c_intrinsic(name, fold=None)``
    Decorator factory. A function marked ``@c_intrinsic('foo')`` is
    lowered by the front-end to a call of the C function ``foo``
    instead of executing the Python body. The Python body is
    typically just ``...`` (ellipsis) and is never run. The C
    function must exist in the ansi-c library (CBMC will link it
    at goto-program finalisation time) and its argument and return
    types must match the Python signature.

    The optional ``fold`` keyword enables parse-time constant
    folding. When ``fold='sqrt'`` is set and every argument at the
    call site is a compile-time constant of a recognised numeric
    kind (float / int), the front-end evaluates the named
    host-side folder (for floats, one of std::<name> from
    <cmath>) and replaces the call with the resulting constant.
    Non-constant arguments fall through to the C call unchanged.
    ``fold`` is a pure optimisation: the runtime answer must be
    identical to the unfolded result.

    Recognised ``fold`` values (2026-05): the monadic float
    functions ``sqrt``, ``cbrt``, ``exp``, ``exp2``, ``expm1``,
    ``log``, ``log2``, ``log10``, ``log1p``, ``sin``, ``cos``,
    ``tan``, ``asin``, ``acos``, ``atan``, ``sinh``, ``cosh``,
    ``tanh``, ``asinh``, ``acosh``, ``atanh``, ``ceil``,
    ``floor``, ``trunc``, ``fabs``, ``erf``, ``erfc``, ``gamma``,
    and ``lgamma``. Anything else is ignored with a silent
    no-op, preserving forward compatibility.

    Example:

        from __cbmc__ import c_intrinsic

        @c_intrinsic('sin', fold='sin')
        def sin(x: float) -> float: ...

    At parse time, ``sin(0.0)`` is folded to ``0.0`` directly;
    ``sin(x)`` (symbolic ``x``) routes through the C library.
"""


def c_intrinsic(name, fold=None):
    """Decorator factory — see module docstring. No runtime effect;
    the front-end consumes the decorator by AST pattern.
    """
    def _decorator(func):
        return func

    return _decorator
