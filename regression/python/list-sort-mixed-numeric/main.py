# PLR §6.4.6 / §3.2.1: list.sort() orders mixed int/float by numeric
# value across the tagged union (not by type tag), preserving each
# element's original object, and is stable for equal values.
lst = [3, 1.5, 2, 0.5]
lst.sort()
assert lst == [0.5, 1.5, 2, 3]

# Stability: equal values (2.0 and 2) keep input order.
lst2 = [2.0, 2, 1]
lst2.sort()
assert lst2 == [1, 2.0, 2]

# Pure-int and all-float still sort correctly.
lst3 = [3, 1, 2]
lst3.sort()
assert lst3 == [1, 2, 3]

lst4 = [3.0, 1.5, 2.0]
lst4.sort()
assert lst4 == [1.5, 2.0, 3.0]
