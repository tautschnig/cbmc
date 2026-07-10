# CORE no-false-alarm companion: appending a MATCHING int into list[int] must
# still verify (the tag-preserving append path must not over-approximate a
# correct program into failure).
xs: "list[int]" = []
xs.append(5)
assert isinstance(xs[0], int)
