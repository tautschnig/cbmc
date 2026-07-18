# Print-sink f-string ELISION (perf whole-group): print(f"...") never
# builds the string (nothing observes it; the string-building intrinsics
# dominated the string solver on print-heavy code -- aws_untagged: 20 GB
# -> 5 GB of refinement state). PLR-preserving: the EMBEDDED expressions
# still convert, so their checks fire -- the missing-key subscript below
# must still raise exactly as CPython does.
d = {"a": 1}
k = "missing"
print(f"benign {d['a']}")  # in-fstring subscript: fine
print(f"boom {d[k]}")  # KeyError -- must still be reported
assert True
