# PLR 3.1/3.3.2: a method accesses fields of an instance whose class is
# defined LATER in the file, via an UNANNOTATED parameter (a python_value).
# The 1a-bis re-pass (third condition) re-converts such methods after all
# class structs are registered, so `item.price`/`item.qty` resolve instead
# of baking to nondet. Not descriptor-specific — the whole group of
# forward-referenced field access through a generic parameter.
class Processor:
    def total(self, item) -> int:
        return item.price + item.qty


class Item:
    def __init__(self, p: int, q: int):
        self.price = p
        self.qty = q


proc = Processor()
it = Item(10, 5)
assert proc.total(it) == 15
