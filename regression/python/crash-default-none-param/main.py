class Node:
    def __init__(self, value: int, next_node=None) -> None:
        self.value = value

n = Node(42)
assert n.value == 42
