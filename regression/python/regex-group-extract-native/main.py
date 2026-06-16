# Precise Match.group(n) capture-group extraction via the native str.++
# decomposition (Phase 3). Precise for literal-pinned capture groups on a
# constant match text; sound nondet otherwise. Requires --python-smt-strings.
import re

# Literal-separated groups: the '-' pins both boundaries -> precise.
m = re.fullmatch("(\\d+)-(\\d+)", "12-34")
assert m is not None
assert m.group(0) == "12-34"
assert m.group(1) == "12"
assert m.group(2) == "34"

# Literal runs around / between groups.
m2 = re.fullmatch("id=(\\w+);v=(\\d+)", "id=abc;v=7")
assert m2 is not None
assert m2.group(1) == "abc"
assert m2.group(2) == "7"

# search with a fixed-length pattern: the span folds, so groups are precise.
m3 = re.search("(\\d\\d)-(\\d\\d)", "xx12-34yy")
assert m3 is not None
assert m3.group(1) == "12"
assert m3.group(2) == "34"

# A group is always within its sub-pattern's language, even when the split is
# ambiguous (adjacent variable-length groups) -- the str.in_re constraint holds.
m4 = re.fullmatch("(\\d+)(\\d+)", "1234")
assert m4 is not None
assert len(m4.group(1)) >= 1
