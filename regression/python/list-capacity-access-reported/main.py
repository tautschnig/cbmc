# Access-level capacity catch-all: augmented `+=` has no construction-time
# guard, so it produces a length-80 list whose data array is only 64 slots.
# Reading a VALID in-length index that lies past the modelled array (a[70],
# 70 < 80 but 70 >= PYTHON_MAX_LIST_LENGTH = 64) is caught at the access as a
# python-model-bound violation instead of silently returning unmodelled data.
# Raising --max-list-length would clear it.
a = [0] * 40
a += [0] * 40
x = a[70]
