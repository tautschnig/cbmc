s = nondet_string(3)
assume(s == "\x1ca\x1c")
assert s.strip() == "a"          # \x1c IS python whitespace (FS)
t = nondet_string(3)
assume(t == "\x1ba\x1b")
assert t.strip() == "\x1ba\x1b"  # \x1b (ESC) is NOT whitespace
