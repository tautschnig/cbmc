# Verify that calling a method that doesn't exist on a known
# class raises a static attribute-error property — even when
# the call is inside a try/except clause that only catches a
# specific (non-AttributeError) exception class.
#
# Generic catch-alls like `except Exception:` are NOT considered
# to "catch" a missing-method bug at the static-detection
# level, since that pattern is typically used for last-resort
# logging rather than for masking statically-known typos.

class MyClient:
    value: int

    def __init__(self) -> None:
        self.value = 0

    def good_method(self) -> int:
        return self.value


class CustomError(Exception):
    pass


client = MyClient()

# 1. Try/except for an unrelated specific exception — bug should
# still be reported.
try:
    client.does_not_exist()  # missing method
except CustomError:
    pass
