# PLR §3.3.1: unpacking a non-iterable class instance (a, b = C(), where C has no
# __iter__ and no __getitem__) raises TypeError ('cannot unpack non-iterable').
# Same whole-group check as the for-loop / comprehension iteration sites.
class C:
    pass


a, b = C()
