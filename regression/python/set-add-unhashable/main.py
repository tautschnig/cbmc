# PLR §3.2: set elements must be hashable; set.add of a list -> TypeError.
s = set()
s.add([1])
