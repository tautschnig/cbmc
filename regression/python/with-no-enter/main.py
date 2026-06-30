# PLR §8.5: the context-manager protocol requires __enter__ AND __exit__. A class
# missing __enter__ -> `with C()` raises TypeError. Same dunder-protocol-missing
# whole-group as subscript/iteration/call.
class C:
    def __exit__(self, *a) -> bool:
        return False


with C() as x:
    pass
