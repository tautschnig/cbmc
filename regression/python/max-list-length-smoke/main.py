# Smoke coverage for --python-max-list-length: the flag (a symbolic-container
# capacity tuning knob) is parsed and threaded to the converter; a basic
# list program still verifies with it set.
xs = [1, 2, 3]
xs.append(4)
assert len(xs) == 4
assert sum(xs) == 10
