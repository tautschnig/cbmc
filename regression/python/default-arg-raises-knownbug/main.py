# KNOWNBUG (exception-propagation, PLR §8.7). A function's default argument
# values are evaluated ONCE, when the `def` statement executes (not at call
# time). If a default expression raises, the `def` raises. Here `[][0]` is an
# IndexError, so CPython raises when `def f` executes (before f is ever called).
# The frontend does NOT evaluate default expressions at def-time (they are only
# re-derived at call sites for the value), so the exception is swallowed --
# cbmc proves SUCCESSFUL (false proof). Desired: VERIFICATION FAILED.
#
# Root cause / plan: def-time default evaluation must run wherever a def is
# "executed" -- top-level defs (module pass), nested defs / method bodies
# (convert_statement). A spike (2026-07-10) confirmed the exception CAN be
# emitted, but doing it soundly needs (a) the uncaught-exception assertion that
# the general per-statement check SKIPS for FunctionDef, and (b) correct
# module-init ORDERING so a default reading an already-bound global
# (`def f(a=G[1])`) does not spuriously fault -- the naive module-pass eval saw
# G before its value was established (a false ALARM). Deferred until the module
# init sequencing is addressed. NOTE: every OTHER evaluation site (call args,
# comprehension elements, f-strings, binops, subscripts, literals, boolean/
# conditional operands, walrus, returns, aug values) DOES propagate correctly --
# this def-time-default site is the sole gap found by the exception-propagation
# audit.
def f(a=[][0]):
    return 1
