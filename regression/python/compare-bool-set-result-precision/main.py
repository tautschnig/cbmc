# PLR §6.10.1: `bool` is a subtype of `int`, so an ordered comparison with a bool
# operand compares by integer value (True == 1). PLR §3.2: set ordering is the
# subset partial order. Both comparison RESULTS are now modelled precisely (were
# sound false alarms).
assert (True < 2) == True
assert (False < True) == True
assert (True >= 1) == True
assert (True < 2.5) == True
assert (0 < True) == True
assert (True < 1) == False

assert ({1} < {1, 2}) == True        # proper subset
assert ({1, 2} <= {1, 2}) == True    # subset (equal)
assert ({1, 2} < {1, 2}) == False    # not a PROPER subset
assert ({1, 2} > {1}) == True
assert ({1, 3} < {1, 2}) == False    # not a subset
