# PLR control-flow correctness: per-branch snapshot + merge of
# conversion-time tracking maps.
#
# Conversion-time tracking maps (string_constants, dict_literals,
# list_literals, tuple_literals, float_constants, alias_targets)
# previously had path-insensitive last-write-wins semantics:
# both arms of an if/else were processed sequentially, and the
# later arm's writes leaked into the merged state, producing
# wrong constant-folds at downstream reads.
#
# This regression test exercises the snapshot+merge:
#   - snapshot before the if
#   - process if-arm with isolated state
#   - capture if-arm post-state
#   - restore snapshot
#   - process else-arm
#   - capture else-arm post-state
#   - merge: keep entries that both arms agree on, drop the rest
#
# The test forces NON-CONSTANT conditions so the if-condition
# can't be folded away — the merge is the only reason the
# downstream reads are correct.

def dict_in_branch(cond: bool) -> None:
    if cond:
        d = {"a": 1, "b": 2}
    else:
        d = {"a": 99, "b": 100}
    if cond:
        # Inside the if-arm again, downstream constant-fold of
        # d["a"] should NOT produce 99 (the else-arm value),
        # because the merge dropped d.
        assert d["a"] == 1


def list_in_branch(cond: bool) -> None:
    if cond:
        xs = [1, 2, 3]
    else:
        xs = [99, 100, 101]
    if cond:
        assert xs[0] == 1


def float_in_branch(cond: bool) -> None:
    if cond:
        y = 1.5
    else:
        y = 99.5
    if cond:
        assert y == 1.5


def string_in_branch(cond: bool) -> None:
    if cond:
        r = "two"
    else:
        r = "other"
    assert r == "two" if cond else r == "other"


# Constant-true branch — the merge should still produce correct
# downstream behaviour because the if-arm and else-arm states
# are merged AFTER each is processed in isolation.
def both_arms_same() -> None:
    cond = True
    if cond:
        x = 42
    else:
        x = 42
    # Both arms set x = 42; merge keeps it.
    assert x == 42


dict_in_branch(True)
list_in_branch(True)
float_in_branch(True)
string_in_branch(True)
both_arms_same()
