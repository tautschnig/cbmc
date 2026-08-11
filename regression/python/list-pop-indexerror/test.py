# Regression: list.pop() on an empty list and list.pop(i) with i out
# of range raise IndexError per Python semantics.
l: list[int] = []
l.pop()
