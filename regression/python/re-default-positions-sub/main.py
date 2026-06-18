# The DEFAULT (refined-string) backend decides regex match POSITIONS
# (Match.start/end/span/group(0)) and re.sub (replace-all) precisely for a
# CONSTANT pattern + CONSTANT subject -- no SMT String solver / --cvc5 needed.
# (re.findall / re.split, which chain positions through a loop, stay a sound
# over-approximation on the default backend; see the plans doc.)
import re

m = re.search("[0-9][0-9]", "ab12cd")
assert m is not None
assert m.start() == 2
assert m.end() == 4
assert m.group(0) == "12"

f = re.fullmatch("[a-z][a-z][a-z]", "abc")
assert f.start() == 0
assert f.end() == 3

assert re.search("[0-9][0-9]", "abcdef") is None

# re.sub replace-all.
assert re.sub("[0-9]", "#", "a1b2") == "a#b#"
assert re.sub("a", "X", "banana") == "bXnXnX"
assert re.sub("z", "Q", "abc") == "abc"
