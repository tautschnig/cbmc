# String concatenation in a loop
word: str = ""
for c in ["a", "b", "c"]:
    word = word + c
assert len(word) == 3
