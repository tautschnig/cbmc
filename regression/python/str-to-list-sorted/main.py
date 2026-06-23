# PLR: list()/reversed()/sorted() iterate a string's code points.
assert list("abc") == ["a", "b", "c"]
assert list(reversed("abc")) == ["c", "b", "a"]
assert sorted("cba") == ["a", "b", "c"]
assert sorted("dcba", reverse=True) == ["d", "c", "b", "a"]
assert sorted("hello") == ["e", "h", "l", "l", "o"]
assert len(list("hello")) == 5
# list of a real list still works
assert sorted([3, 1, 2]) == [1, 2, 3]
