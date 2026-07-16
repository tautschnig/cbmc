# Method-call argument tag obligation (PLR §3.2 / type-safety): binding an
# Any/union value to an EXPLICITLY-ANNOTATED scalar METHOD parameter is not a
# runtime coercion -- a str-tagged value bound to an `int` method param and used
# as int raises TypeError. The provenance-gated call-argument obligation that
# fires for free functions now also fires at method-call sites (the main method
# path routes args through coerce_call_argument, not raw safe_typecast).
# 2026-07-16: the obligation is now a CONDITIONAL, CATCHABLE TypeError at the
# binding (the boundary approximation of the callee-internal raise) plus a
# guarded nondet bind -- the property that fires is the uncaught-exception one
# (the old hard call-site assert false-alarmed on unknowable tags in real code).
from typing import Any


class Foo:
    def bar(self, n: int) -> int:
        return n + 1


def f(x: Any) -> int:
    obj = Foo()
    return obj.bar(x)   # str bound to int method param -> TypeError


f("oops")
