# PLR §2.4.5: zip(iter1, iter2) pairs elements
a = [1, 2, 3]
b = [4, 5, 6]
pairs = list(zip(a, b))
assert len(pairs) == 3
