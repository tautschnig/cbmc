import random


# Native SMT-String back-end: upper()/lower() are modelled precisely via a
# per-character str.to_code / ASCII-arithmetic / str.from_code mapping.
assert "abc".upper() == "ABC"
assert "ABc".lower() == "abc"
assert "Hello, World!".upper() == "HELLO, WORLD!"
assert "MiXeD123".lower() == "mixed123"

# Symbolic, pinned via assume: the mapping holds for a constrained subject.
s = random.choice(["Hi", "Yo"])
if s == "Hi":
    assert s.upper() == "HI"
    assert s.lower() == "hi"
