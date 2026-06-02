# PLR §3.2: a list is unhashable, so using one as a dict key raises
# TypeError.
d = {}
k = [1, 2]
d[k] = 1
