# CORE (2026-07-13): a function's default argument values are evaluated ONCE, at
# def-time (when the `def` executes), NOT at call time. If a default expression
# raises, the `def` raises. Here `[][0]` is an IndexError, so CPython raises when
# `def f` executes (before f is ever called). The frontend now evaluates each
# default at the def's SOURCE-ORDER position (module pass for top-level defs,
# convert_statement for nested defs, ClassDef handling for methods) and emits the
# uncaught-exception assertion the general per-statement check omits for
# FunctionDef/ClassDef. Evaluating in source order means a default reading an
# already-bound global does not spuriously fault (see default-arg-ordering-nofp).
# CPython raises -> VERIFICATION FAILED.
def f(a=[][0]):
    return 1
