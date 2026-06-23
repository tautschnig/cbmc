import re
m = re.match("(?P<x>a)", "a")
# was a false proof (groupdict returned {}); CPython gives {'x':'a'}
assert m.groupdict() == {}
