def add_elem(s):
    # s is unannotated -> Any. A set is a fixed bitmap struct shared by
    # reference (SET tag via __class_ptr), so mutators propagate to the caller.
    s.add(5)


a = {1, 2}
add_elem(a)
assert 5 in a


def drop(s):
    s.discard(1)


b = {1, 2}
drop(b)
assert 1 not in b
