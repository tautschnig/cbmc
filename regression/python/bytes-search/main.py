# bytes.count and bytes.index search a subsequence (or a byte), from a start to
# an end; they searched for an element equal to the bytes argument.
assert b"a b a".count(b"a") == 2
assert b"ab".index(b"b") == 1
assert b"abab".count(b"ab", 1) == 1
assert b"aaa".count(b"aa") == 1
assert b"ab".count(b"") == 3
assert b"ab".count(97) == 1
try:
    b"ab".index(b"c")
    assert False
except ValueError:
    pass
