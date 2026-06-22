# Native SMT-String back-end: sound segment-count bounds for split on a
# symbolic subject. split with an EXPLICIT separator always returns >= 1
# element (even "".split(",") == ['']) and at most len(s)+1 elements (a
# separator has length >= 1). The previous nondet fallback allowed count 0 and
# an unbounded count, so these always-true relations falsely FAILed.
s = nondet_string()

# explicit separator: count in [1, len(s)+1]
parts = s.split(",")
assert len(parts) >= 1
assert len(parts) <= len(s) + 1
