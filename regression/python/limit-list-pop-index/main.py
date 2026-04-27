# PLR §4.6.1: list.pop(i) removes and returns element at index i
lst = [1, 2, 3]
x: int = lst.pop(0)
assert x == 1
assert len(lst) == 2
