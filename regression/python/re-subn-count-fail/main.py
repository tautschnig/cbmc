import re
r, n = re.subn("a", "b", "aaa")
# was a false proof (count returned 0); CPython gives 3
assert n == 0
