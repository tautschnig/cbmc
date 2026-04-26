# PLR §4.6.1: list.pop() on heterogeneous list
lst = [1, 2, 3]
x: int = lst.pop()
assert x == 3
assert len(lst) == 2
