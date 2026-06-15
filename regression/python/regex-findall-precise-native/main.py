# Precise re.findall for a fixed-length pattern on a constant subject, via the
# match-position intrinsics. Enabled by: the pattern/string param annotations
# (so the literal subject folds into the intrinsics), the list[str] element
# typing, and the symbolic-`from` position lowering.
import re

assert len(re.findall("[0-9][0-9]", "a12b34")) == 2
r = re.findall("[0-9][0-9]", "a12b34")
assert r[0] == "12"
assert r[1] == "34"
assert len(re.findall("[0-9][0-9]", "ab12cd")) == 1
assert len(re.findall("[0-9][0-9]", "abcdef")) == 0
