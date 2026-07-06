# KNOWNBUG (PLR §6.3.2/§3.2): the DIRECT mixed concat `min([8,8] + ["c"])` is now
# CAUGHT (list-concat-fold-mixed CORE). This VARIABLE-reassignment form is not:
# `xs = [8,8]` binds xs as list[int]; `xs = xs + ["c"]` folds to a promoted
# list[python_value] literal, but the reassignment CASTS it back to xs's original
# list[int] slot (through a layered coercion path), dropping the str so the
# mixed-category TypeError is hidden. Closing it needs list-element retype on
# reassignment (the promoted element type must survive rebinding). Found by the
# property-based random fuzzer (rand_169).
xs = [8, 8]
xs = xs + ["c"]
r = min(xs)
