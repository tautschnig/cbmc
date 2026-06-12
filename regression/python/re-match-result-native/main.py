# Native SMT-String backend: re.compile(<literal>).match/search/fullmatch
# now returns a real Match-or-None reflecting the SMT regex decision, rather
# than always-Match. (Precise under --cvc5 --python-smt-strings.)
import re

assert re.compile("abc").match("abcxyz") is not None      # matches at start
assert re.compile("xyz").match("abcxyz") is None          # no match at start
assert re.compile("bcx").search("abcxyz") is not None     # matches anywhere
assert re.compile("zzz").search("abcxyz") is None         # not present
assert re.compile("abc").fullmatch("abc") is not None     # exact
assert re.compile("abc").fullmatch("abcd") is None        # not full
