# PLR §5.6.1: `del lst[i]` removes element i and shifts subsequent
# elements down. After the shift, the slot at the new length is
# stale — it still holds the value that was at the old length-1
# position. Without zeroing it, struct-level equality with a
# freshly-built list literal (whose data array is zero-padded past
# its length) would mismatch on that slot.

def test_del_middle() -> None:
    lst = [1, 2, 3]
    del lst[1]
    assert lst == [1, 3]

def test_del_head() -> None:
    lst = [1, 2, 3]
    del lst[0]
    assert lst == [2, 3]

def test_del_tail() -> None:
    lst = [1, 2, 3]
    del lst[2]
    assert lst == [1, 2]

test_del_middle()
test_del_head()
test_del_tail()
