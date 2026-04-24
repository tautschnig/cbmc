# Limitation: None modeled as 0, so 0 is None is True (wrong)
x = None
assert x is None
assert 0 is not None
