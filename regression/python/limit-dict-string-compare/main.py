# PLR §6.10: dict value comparison should detect mismatch
# When dict is accessed via subscript, string content must be tracked
d: dict = {"ref": "Python"}
assert d["ref"] == "Pythonn"  # should FAIL — extra 'n'
