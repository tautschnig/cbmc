# PLR 8.4: try/except/finally semantics.
#
# Key invariants:
#   1. finally ALWAYS runs.
#   2. except runs iff a matching exception was raised in
#      the try body.
#   3. else runs iff NO exception was raised in the try
#      body (note: NOT when except caught one).
#   4. raise inside a try body is caught by a matching
#      except in the SAME function (not propagated to
#      caller via early return).


# Case 1: no exception → finally runs.
log1 = []
def no_exc():
    try:
        log1.append(1)
    finally:
        log1.append(2)

no_exc()
assert len(log1) == 2
assert log1[0] == 1
assert log1[1] == 2


# Case 2: exception caught → except then finally.
log2 = []
def caught():
    try:
        log2.append(1)
        raise ValueError("x")
    except ValueError:
        log2.append(2)
    finally:
        log2.append(3)

caught()
assert len(log2) == 3
assert log2[0] == 1
assert log2[1] == 2
assert log2[2] == 3


# Case 3: no exception → else runs, then finally.
log3 = []
def with_else():
    try:
        log3.append(1)
    except ValueError:
        log3.append(99)
    else:
        log3.append(2)
    finally:
        log3.append(3)

with_else()
assert len(log3) == 3
assert log3[0] == 1
assert log3[1] == 2
assert log3[2] == 3


# Case 4: exception caught → else is SKIPPED, finally runs.
log4 = []
def caught_no_else():
    try:
        log4.append(1)
        raise ValueError("x")
    except ValueError:
        log4.append(2)
    else:
        log4.append(99)
    finally:
        log4.append(3)

caught_no_else()
assert len(log4) == 3
assert log4[0] == 1
assert log4[1] == 2
assert log4[2] == 3
# 99 must NOT be in log4 — else must not run when except fired.
