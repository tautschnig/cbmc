# --python-check-iter-none: iterating None raises TypeError
# ('NoneType' object is not iterable).
x = None
for i in x:
    pass
