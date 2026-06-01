# §1 (BMC container bound): appending past PYTHON_MAX_LIST_LENGTH (64)
# used to write past the modelled data[64] array and silently corrupt
# state. The growth site now asserts the capacity, so a beyond-capacity
# append is reported (property class python-model-bound) rather than
# mis-modelled.
lst = []
for i in range(70):
    lst.append(i)
assert len(lst) == 70
