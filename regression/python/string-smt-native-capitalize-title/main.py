import random

# Native SMT-String capitalize() and title() (ASCII case mapping with
# position / word-boundary direction; a word starts after any non-letter).
assert "hELLO".capitalize() == "Hello"
assert "hi there".capitalize() == "Hi there"
assert "hello world".title() == "Hello World"
assert "they're bill's".title() == "They'Re Bill'S"
assert "ab1cd".title() == "Ab1Cd"

s = random.choice(["foo bar", "x"])
if s == "foo bar":
    assert s.title() == "Foo Bar"
    assert s.capitalize() == "Foo bar"
