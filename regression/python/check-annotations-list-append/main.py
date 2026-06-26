# --python-check-annotations: appending a definitely-incompatible value to a
# list with a concrete element type (list[int]) is an annotation mismatch
# (the ty-007 laundering shape). Opt-in only -- legal at runtime, so gated
# behind the flag like the call-arg / assign annotation checks.
xs: list[int] = []
v = "s"
xs.append(v)
