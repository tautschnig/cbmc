# PLR §6.1.4: str.format() and str.format_map(map) raise:
#   - IndexError when a positional placeholder index has no
#     matching argument: "{1}".format("a"), "{} {}".format("a")
#   - KeyError when a named placeholder doesn't match a kwarg
#     or a key in format_map's mapping: "{n}".format(),
#     "{x}".format_map({"y": 1})
#
# Companion to the regression-suite IndexError / KeyError
# detection. The fail/ test verifies the missing-arg case
# triggers an uncaught-exception assertion.

# IndexError-on-format with valid index works
"{0}".format("a")
"{}".format("a")

# format_map with all keys present works
"{x}".format_map({"x": 1})

# format with kwarg works
"{n}".format(n="hi")
