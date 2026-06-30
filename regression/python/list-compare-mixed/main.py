# PLR 6.10.1: lexicographic list ordering compares element-wise; at the first
# position where a comparison actually happens, comparing a number with a str
# raises TypeError. Here positions 0 are equal (1 == 1), so position 1 (2 < "a")
# is reached -> TypeError.
b = [1, 2] < [1, "a"]
