# PLR §8.5: the context-manager protocol requires __enter__ AND __exit__. A class
# missing __exit__ -> `with C()` raises TypeError.
class C:
    def __enter__(self) -> "C":
        return self


with C() as x:
    pass
