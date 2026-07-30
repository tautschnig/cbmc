# Pass-0.5 pre-registration typed a module-level list's elements by a
# 'value[0] != b' heuristic -- any STRING starting with 'b' (e.g.
# "backup-...") was misread as bytes, degrading the pre-registered
# element type and PUNNING the layout against the pass-2 assignment:
# function-scope reads of the elements saw garbage. Bytes constants
# serialize as b'..'/b".." -- the discriminator must check the quote.
BUCKETS = ["backup-service", "auth-service", "billing-service"]
BLOBS = [b"hi", b"yo"]


def f():
    s = BUCKETS[0]
    assert len(s) == 14
    assert BUCKETS[0] == "backup-service"
    assert len(BLOBS) == 2


f()
