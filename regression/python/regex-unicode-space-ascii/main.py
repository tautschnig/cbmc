import re
# PLR: \s is Unicode whitespace; within ASCII that includes \x1c-\x1f
# (FS GS RS US). \x1b (ESC) is NOT whitespace. Dot matches \r (only \n
# excluded). Pins the dialect-corrected classes on the native backend.
s = nondet_string(1)
assume(s == "\x1c")
assert re.fullmatch("\\s", s) is not None
t = nondet_string(1)
assume(t == "\x1b")
assert re.fullmatch("\\s", t) is None
u = nondet_string(3)
assume(u == "a\rb")
assert re.fullmatch("a.b", u) is not None
