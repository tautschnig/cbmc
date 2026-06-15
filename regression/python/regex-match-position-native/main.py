# Native SMT-String match-position intrinsic (Phase 1): precise
# Match.start/end and group(0) for a fixed-length pattern on a constant
# subject. The leftmost-start scan folds because the subject is a literal;
# variable-length patterns / symbolic subjects degrade to a sound nondet floor
# (covered elsewhere). See doc/python-frontend-regex-position-plan.md.
import re

m = re.search("[0-9][0-9]", "ab12cd")
assert m is not None
assert m.start() == 2
assert m.end() == 4
assert m.group(0) == "12"
assert m.group() == "12"   # no-arg form (method default filled)

# fullmatch: the whole string is the match.
f = re.fullmatch("[a-z][a-z][a-z]", "abc")
assert f is not None
assert f.start() == 0
assert f.end() == 3
assert f.group(0) == "abc"

# compiled pattern; leftmost match is the first [a-z][a-z] run.
p = re.compile("[a-z][a-z]")
mm = p.search("12abcd")
assert mm.start() == 2
assert mm.group(0) == "ab"

# no match -> None.
assert re.search("[0-9][0-9]", "abcdef") is None
