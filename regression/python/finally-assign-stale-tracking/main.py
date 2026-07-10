# PLR §8.4.2: the finally body runs on all paths and is the LAST code executed,
# so a name it reassigns is definitely updated afterwards. The try arm-merge
# discarded the finalbody's tracking updates, so `b = 4; try:... finally: b = 1`
# left the stale float_constants[b]=4, and `range(b)` folded on 4 (giving [0..3])
# instead of [0] -- a false proof found by the mutation-oracle. Tracking for
# finalbody-assigned names is now cleared after the merge. r is [0], so `!= [0]`
# raises.
b = 4
try:
    a = 1
finally:
    b = 1
r = [i for i in range(b)]
assert r != [0]
