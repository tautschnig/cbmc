# An empty slice has length 0; normal/negative/full slices are unaffected.
assert len([5, 6][2:1]) == 0
assert [5, 6][2:1] == []
assert "abc"[2:1] == ""
assert [5, 6, 7][1:2] == [6]
assert [1, 2, 3, 4][1:-1] == [2, 3]
