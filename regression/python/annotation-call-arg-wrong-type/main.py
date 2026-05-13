# PLR soundness test: caller passes wrong-typed argument
# to annotated parameter.
#
# Python semantics: type hints are not runtime-enforced.
# f("hello") is legal at call time; the TypeError happens
# inside f's body at 'x + 1'. Our static analysis previously
# silently typecast str to int at the call site, missing the
# mismatch.
#
# With annotation checking, this emits an 'annotation-mismatch'
# property at the call.

def f(x: int) -> int:
    return x + 1

result = f("hello")  # type: ignore
