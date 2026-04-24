# Limitation: no virtual method dispatch
class Base:
    def value(self) -> int:
        return 1

class Derived(Base):
    def value(self) -> int:
        return 2

def get_value(obj: Base) -> int:
    return obj.value()

d = Derived()
assert get_value(d) == 2
