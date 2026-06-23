import re
m = re.match("(a)(b)", "ab")
# was a false proof (groups returned ()); CPython gives ('a','b')
assert m.groups() == ()
