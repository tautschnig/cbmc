# --python-check-annotations (P3): a cross-module call mod.foo(5) where the
# imported foo declares `a: str` is an argument-type mismatch (int vs str).
# The module-call dispatch now runs the same annotation check as local calls.
import mod

mod.foo(5)
