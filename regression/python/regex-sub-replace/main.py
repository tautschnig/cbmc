import re

# Precise re.sub under --python-smt-strings on the sound subset: a constant,
# fixed-length-(>=1) pattern with a literal replacement and the default
# (replace-all) count. SMT-LIB str.replace_re_all coincides with CPython
# re.sub exactly here (forced match length => greedy == leftmost-shortest,
# and no empty matches).
assert re.sub("foo", "BAR", "foofoof") == "BARBARf"
assert re.sub("[0-9]", "#", "a1b2") == "a#b#"
assert re.sub("[0-9][0-9]", "N", "12345") == "NN5"
assert re.sub("xyz", "Q", "abc") == "abc"            # no match: unchanged
assert re.compile("ab").sub("X", "abab") == "XX"     # compiled pattern

# Precise on a symbolic subject too.
s = input()
assume(s == "a1b2c3")
assert re.sub("[0-9]", "_", s) == "a_b_c_"
