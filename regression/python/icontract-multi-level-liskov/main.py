# Phase 7 multi-level Liskov: contracts compose transitively
# across grandparent/parent/child chains.

import icontract


# Grandparent.
class A:
    @icontract.ensure(lambda result: result >= 0)
    def m(self, x: int) -> int:
        return x * x


# Parent — strengthens the postcondition further.
class B(A):
    @icontract.ensure(lambda result: result <= 1000)
    def m(self, x: int) -> int:
        if x > 30:
            return 1000
        if x < 0:
            return 0
        return x * x


# Child — adds yet another strengthening. Per Liskov, C.m
# must satisfy ALL THREE postconditions: A's >= 0, B's <=
# 1000, and C's >= 1.
class C(B):
    @icontract.ensure(lambda result: result >= 1)
    def m(self, x: int) -> int:
        if x > 30:
            return 1000
        if x < 1:
            return 1
        return x * x


# Multi-level invariant inheritance.
@icontract.invariant(lambda self: self.value >= 0)
class P:
    def __init__(self, x: int) -> None:
        self.value = x


@icontract.invariant(lambda self: self.value <= 100)
class Q(P):
    pass


@icontract.invariant(lambda self: self.value % 2 == 0)
class R(Q):
    pass


def main():
    # Method chain.
    c = C()
    assert c.m(5) == 25
    assert c.m(0) == 1   # clamped by C
    assert c.m(50) == 1000  # clamped by B (via C inheriting)

    # Three-level invariant chain — value 50 satisfies all.
    r = R(50)
    assert r.value == 50


main()
