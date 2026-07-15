# CORE: a method call through a CLASS-LEVEL-annotated field (`client: Svc` in the
# class body) reads the receiver via the shadow-fallback TERNARY; taking a plain
# address_of of that if-expression for `self` CRASHED symex ("address_arithmetic:
# either non-persistent array or pointer to result") -- the root cause of the
# real-world boto3-benchmark crash whole-group (8/51 programs). safe_address_of
# now distributes the address-of over the ternary (both arms are genuine
# lvalues, preserving reference semantics). The value flows correctly (start()
# returns 5).
class Svc:
    def start(self) -> int:
        return 5


class AC:
    client: Svc

    def __init__(self) -> None:
        self.client = Svc()


a = AC()
assert a.client.start() == 5
