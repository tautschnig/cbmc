# Smoke coverage for --python-max-string-length: the flag (a symbolic-string
# capacity tuning knob) is parsed and threaded to the converter; a basic string
# program still verifies with it set.
s = "hello"
t = s + " world"
assert len(t) == 11
