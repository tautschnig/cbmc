# Free-function **kwargs packing under the native SMT-String backend:
# unmatched keyword names are interned to string-id handles via
# coerce_element (a raw smt_string in the handle-typed keys array
# violated simplify_rec's type postcondition -- the free-function twin
# of the method-path kwargs site, found by documentation/code review
# rather than the corpus, which only exercises the method path).
def f(a: int, **kwargs) -> int:
    if "mode" in kwargs:
        return 1
    return 0


r = f(1, mode="fast", level="high")
assert r == 1
assert f(2) == 0

# dict() CONSTRUCTOR kwargs: the same interning applies at the dict(a=1)
# builder (a third raw-literal keys-array site, plus two sentinel-dict
# builders in the assign paths, fixed as one group).
d = dict(a=1, b=2)
assert "a" in d
assert d["b"] == 2
