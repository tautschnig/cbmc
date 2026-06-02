# PLR §3.2: a list is unhashable, so using one as a set element raises
# TypeError.
s = {[1, 2]}
