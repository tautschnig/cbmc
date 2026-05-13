# PLR soundness test: function body returns wrong-typed value
# vs. declared return annotation.
#
# Python semantics: annotations are not enforced at runtime.
# f() returns the string "hello" regardless of the '-> int'
# annotation. Downstream operations like 'x + 5' would then
# raise TypeError at runtime.
#
# Without annotation checking, our frontend trusts the
# annotation and misses that the body's return diverges.
#
# With annotation checking, this emits an 'annotation-mismatch'
# property at the return statement.

def f() -> int:
    return "hello"  # mismatch: body returns str, annotation says int

x = f()
