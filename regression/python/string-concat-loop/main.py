# PLR §6.5.6: str.__add__ inside a loop. The augmented-assign
# `word += char` on string operands routes through the
# refinement-string solver so the post-loop assertion can be
# discharged against the concatenated content.
word: str = ""
s: str = "a"
for char in s:
    word += char
assert word == "a"

# Plain (non-loop) constant-string concatenation still constant
# folds.
hello: str = ""
hello += "Hi"
assert hello == "Hi"
