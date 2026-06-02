# PLR semantics: a dict has unbounded capacity, so 17 distinct keys
# yield len == 17. CBMC's dict model has only 16 slots
# (PYTHON_MAX_DICT_SIZE), so the 17th insertion trips the
# python-model-bound capacity guard and the program cannot be verified.
# This is the foundational BMC container bound (differential2 §1):
# sound (the over-capacity execution is reported, not silently
# trusted), but it prevents establishing the PLR-true property.
d = {}
for i in range(17):
    d[str(i)] = i
assert len(d) == 17
