class Node:
    def __init__(self, value: int, successor=None) -> None:
        self.value = value
        self.successor = successor

a = Node(1)
b = Node(2, a)
assert b.value == 2
