# KNOWNBUG (false proof, per-instance-identity cluster): direct aliasing of a
# class instance. `b = a` should bind the SAME object (like list/dict aliasing),
# so a mutation through one alias is visible through the other. cbmc uses value
# semantics for class instances at a top-level `b = a` assignment (struct copy),
# so b.x = 99 does not affect a.x and cbmc proves the stale value.
#
# CPython: a.x is 99 after b.x = 99 -> the assert is false.
#
# This is a SIMPLER sibling of shared-object-aliasing-knownbug (which shares an
# instance via a constructor parameter). The list/dict alias_targets pointer
# mechanism does NOT trivially extend here: instance attribute read/write does
# not route through the alias pointer the way list subscript does (the
# per-instance-identity / attribute-field-reference problem). The viable sound
# fix is a havoc-on-mutation guard (analogous to the extraction-then-mutate
# guard): record `b = a` instance aliases and havoc the other alias on a
# mutation through either. Tracked for a focused follow-up.
class S:
    def __init__(self) -> None:
        self.x: int = 42


a = S()
b = a
b.x = 99
assert a.x == 42   # FALSE in CPython (a.x is 99)
