# §1: appends within the model capacity (64) must not trip the
# capacity guard — verification succeeds normally.
lst = []
for i in range(10):
    lst.append(i * 2)
assert len(lst) == 10
assert lst[5] == 10
