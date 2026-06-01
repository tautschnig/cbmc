# Differential unsoundness witness §4 (cbmc-py-differential):
# `with` calls __enter__ but never emits the __exit__ call, so any
# assertion/bug inside __exit__ is never checked, and exception
# suppression (a __exit__ returning True) is not modeled.
# CPython runs __exit__ at block exit, so the assert False fires
# (AssertionError). cbmc-py currently reports SUCCESSFUL (false
# negative). KNOWNBUG until convert_with emits the __exit__ call.
class CM:
    def __enter__(self) -> "CM":
        return self
    def __exit__(self, a, b, c) -> bool:
        assert False
        return False
with CM() as m:
    x = 1
