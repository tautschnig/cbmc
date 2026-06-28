# Reference semantics: an instance passed to a function -- whether directly, via
# a plain alias `r = o`, or via an ANNOTATED alias `r: C = o` -- shares identity,
# so the callee's mutation propagates back to the caller's object (PLR §3.1).
# Two fixes converge here:
#   1. boxing an LVALUE instance (symbol/deref/member/index) into an Any
#      parameter preserves identity (address_of) instead of a throwaway copy;
#   2. an annotated alias `r: C = o` pointer-promotes like list/dict.
# Previously the alias was boxed as a copy and the mutation was lost
# (a2_narrowing_alias false proof).


class Holder:
    def __init__(self) -> None:
        self.d: int = 10


def mutate(h: Holder) -> None:
    h.d = 99


def main() -> None:
    o = Holder()
    r: Holder = o          # annotated alias -- same object
    mutate(r)              # mutates o through the alias-into-call boundary
    assert o.d == 99       # the mutation must be visible on o
    # plain alias too
    s = o
    mutate(s)
    assert o.d == 99


main()
