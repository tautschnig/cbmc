# PLR §3.3.1: iterating an instance whose class defines NEITHER __iter__ nor
# __getitem__ raises TypeError ('object is not iterable'). A class with only
# __getitem__ IS iterable (old sequence protocol), so the check is gated on
# both being absent.
class C:
    pass


for x in C():
    pass
