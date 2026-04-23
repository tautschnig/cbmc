# Crash: from_integer called with unsupported type
# Triggered by any()/all() on non-literal lists, or None values
x = None
assert not any([x])
