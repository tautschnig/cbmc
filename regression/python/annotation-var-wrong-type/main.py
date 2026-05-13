# PLR soundness test: annotated variable assigned a
# wrong-typed value.
#
# Python semantics: annotations on variables are not
# runtime-enforced. 'x: int = "hello"' assigns the string
# to x. Subsequent uses like 'x + 5' would raise TypeError.
#
# Our static analysis previously silently typecast str to
# int at the annotated-assign site, treating x as int
# henceforth.
#
# With annotation checking, this emits an
# 'annotation-mismatch' property.

x: int = "hello"  # type: ignore
