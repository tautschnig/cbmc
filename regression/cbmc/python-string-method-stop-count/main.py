# PLR / str: support the stop-count variants of string methods:
#   str.index(sub, start[, end])  - search slice [start, end)
#   str.replace(old, new[, count]) - max count replacements
#   str.split(sep[, maxsplit])    - max maxsplit splits
#
# Previously, the optional start/end/count/maxsplit arguments
# were silently ignored — index() searched the whole string,
# replace() replaced everything, split() split everything.
# This caused soundness gaps where assertions that should fail
# (e.g. asserting len(split("-", 1)) == 3) were instead reported
# as VERIFICATION SUCCESSFUL.

def index_with_start() -> None:
    s = "banana"
    # start=-2 means start from index 4. "banana"[4:6] = "na".
    # find("na") in "na" = 0; +start(4) = 4.
    assert s.index("na", -2) == 4


def replace_with_count() -> None:
    # count=1: only first occurrence is replaced.
    assert "aa-bb-aa".replace("aa", "x", 1) == "x-bb-aa"


def replace_with_count_zero() -> None:
    # count=0: no replacements happen.
    assert "aa-bb-aa".replace("aa", "x", 0) == "aa-bb-aa"


def split_with_maxsplit() -> None:
    # maxsplit=1: result has 2 parts.
    assert "a-b-c".split("-", 1) == ["a", "b-c"]


def split_with_maxsplit_zero() -> None:
    # maxsplit=0: no splits, single-element list.
    assert "a-b-c".split("-", 0) == ["a-b-c"]


index_with_start()
replace_with_count()
replace_with_count_zero()
split_with_maxsplit()
split_with_maxsplit_zero()
