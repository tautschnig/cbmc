# No false positives: the str-method AttributeError check must NOT fire for a
# real str, for bytes (which HAS upper/lower/...), for SHARED method names that
# lists/tuples genuinely have (count/index), for a user class that defines the
# method, or for an Any-typed receiver that is a str at runtime. (Bare calls
# verify "no spurious AttributeError"; exact return values of count/bytes are
# not asserted here to avoid unrelated modelling precision.)
class HasUpper:
    def upper(self) -> int:
        return 5


def via_any(x):
    return x.upper()


def main() -> None:
    assert "hello".upper() == "HELLO"   # real str
    b"hi".upper()                        # bytes HAS upper -> must not raise
    [1, 2].count(2)                      # list.count (shared name) -> must not raise
    (1, 2, 3).index(2)                   # tuple.index (shared name) -> must not raise
    assert HasUpper().upper() == 5       # user-defined method of that name
    via_any("ok")                        # Any receiver, str at runtime -> must not raise


main()
