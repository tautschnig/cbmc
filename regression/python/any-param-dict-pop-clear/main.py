def remove_key(d):
    d.pop("a")


m = {"a": 1, "b": 2}
remove_key(m)
assert len(m) == 1

# clear() is likewise __tag-dispatched on an Any receiver.
def empty(d):
    d.clear()


n = {"x": 1, "y": 2}
empty(n)
assert len(n) == 0
