# P1c: an operation that still scans a bounded prefix (here: sum)
# must FAIL CLOSED on a list longer than the scan — a loud
# python-model-bound property at the use site, never a silently
# wrong value (PLR: a wrong sum is a false proof). The membership
# and min/max scans carry the same guard.
xs = [i for i in range(20)]
assert sum(xs) == 190
