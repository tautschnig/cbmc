# The structural-metadata invariant under --python-unbounded-ints:
# container length/index/count metadata is ALWAYS signedbv[64] (the
# type declared by python_list_type/python_dict_type), while Python-int
# VALUES are integer_typet. Mixing them built mixed-type if_exprt /
# plus_exprt nodes (symex rename + simplify_rec ABORTS -- crashes, not
# alarms) at: the bytes literal's length op, the negative-index READ
# adjustment, the append/insert length increments, and the
# filtered-copy counter. The i64 length is LIFTED into the index's
# integer domain for adjustments (always value-exact). Remaining on the
# recorded list for this experimental config (per-property solver
# ERROR, not a crash): the negative-index STORE's IndexError formula
# and symbolic-index STRING-element reads.
batch: object = []
batch.append(1)
batch.append(2)
assert len(batch) == 2

ns = [10, 20, 30]
i = 1
assert ns[i] == 20
j = -1
assert ns[j] == 30

bs = b"ab"
assert len(bs) == 2
