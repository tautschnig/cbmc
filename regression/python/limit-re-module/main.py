# PLR stdlib: re module not supported
import re
m = re.match(r"(\d+)", "123abc")
assert m is not None
