# KNOWNBUG (list ==/!= cross-element-type ROUTING). `xs` is list[int] [6, 0] (a
# bool `9 in xs` -> False appended, coerced to int 0); the literal [6, False] is
# list[python_value] (element 1 = python_value{BOOL, cast(false)}). Comparing a
# list[int] to a list[python_value] should reach the cross-element-type BRIDGE
# (which unwraps python_value elements via value_equal/unwrap_value), but for
# this shape it does NOT: the emitted formula is a malformed
# `equal_exprt{xs.length, <whole list-value struct>}` (int vs struct) -- an
# ill-formed struct-level compare -- so `xs != [6, False]` false-proves (cbmc
# SUCCESSFUL; CPython AssertionError since 0 == False). Root is comparison
# ROUTING (the bridge branch is bypassed for list[int] vs list[python_value]
# with a bool element), NOT the element-unwrap itself. Needs the compare
# dispatch to route this to the bridge (or a uniform value_equal on the whole
# list). Found by the mutation-oracle over the widened grammar (rand_123).
xs = [6]
xs.append(9 in xs)
assert xs != [6, False]
