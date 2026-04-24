# Limitation: for c in string yields int char code, not single-char string
word = "abc"
chars = []
for c in word:
    chars.append(c)
assert chars[0] == "a"
