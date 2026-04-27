# PLR §2.4.5: zip(*iterables) — modeled as nondet list
a = [1, 2, 3]
b = [4, 5, 6]
pairs = list(zip(a, b))
# Can't assert content (nondet), but no crash
