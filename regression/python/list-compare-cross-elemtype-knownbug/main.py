# KNOWNBUG (list ==/!= cross-element-type). `xs` is list[int] [6, 0] (a bool
# `9 in xs` -> False appended, coerced to int 0). The literal [6, False] is
# inferred list[python_value] (mixed int + bool). The list `!=` compares a
# list[int] against a list[python_value] element-wise WITHOUT unwrapping the
# boxed values, so it proves `xs != [6, False]` although 0 == False -> a false
# proof (cbmc SUCCESSFUL; CPython AssertionError). Root: list ==/!= needs
# type-aware element comparison (value_equal-style unwrap of python_value), the
# same foundation as dict-by-ref. Found by the mutation-oracle (negated
# value-oracle) fuzzer over the widened grammar (rand_123). Narrow corner: a
# mixed int+bool list literal compared to an int list.
xs = [6]
xs.append(9 in xs)
assert xs != [6, False]
