# PLR §3.3.1: __str__ must return a str. A concretely non-str return type (here
# int) raises TypeError ('__str__ returned non-string') at the str() call.
class C:
    def __str__(self) -> int:
        return 5


s = str(C())
