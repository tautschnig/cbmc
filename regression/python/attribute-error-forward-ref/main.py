# Verify that the attribute-error check does NOT fire for
# forward-references — i.e. when a class method calls another
# method declared LATER in the same class body. Methods are
# converted in source order, so a naive missing-method lookup
# would miss the not-yet-registered later method.
#
# The pre-pass in convert_class_def populates
# class_declared_methods with all methods (and inner classes)
# declared on the class regardless of body-conversion order.

class Manager:
    region: str

    def __init__(self) -> None:
        self.region = "us-east-1"
        # Forward reference: configure() is defined below.
        self.configure()

    def configure(self) -> None:
        pass


# Catch-all for the inner _Exceptions pattern used by boto3-style
# stubs: self._InnerClass() should not be flagged.
class HasInner:
    inner: object

    class _Exceptions:
        pass

    def __init__(self) -> None:
        self.inner = self._Exceptions()


m = Manager()
assert m.region == "us-east-1"
h = HasInner()
