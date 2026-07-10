# PLR §6.10.1: bool is int-compatible, so [0] == [False]. The cross-element-type
# list bridge excluded ID_bool from its numeric-bridgeable check, so list[int] vs
# list[bool] fell to the never-equal trick and `[0] != [False]` false-proved.
# Found by the mutation-oracle. Here `[0] != [False]` must raise. Expected: FAILED.
assert [0] != [False]
