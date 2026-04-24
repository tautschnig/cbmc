# String iteration yields single-char strings
word = "abc"
first: str = ""
for c in word:
    first = c
    break
assert first == "a"
