def remove_one(s):
    # `remove` is shared across list/set, so it is __tag-dispatched at runtime.
    s.remove(1)


a = {1, 2}
remove_one(a)
assert 1 not in a


def empty(s):
    # `clear` is shared across list/dict/set.
    s.clear()


b = {1, 2, 3}
empty(b)
assert len(b) == 0
