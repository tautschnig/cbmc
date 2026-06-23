import re
m = re.match("(a)(b)", "ab")
# was a false proof (expand returned ""); CPython gives "x"
assert m.expand("x") == ""
