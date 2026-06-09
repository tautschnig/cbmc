# Self-referential class fields (linked lists / trees). A field typed
# Optional["List"] is a union (instance reached via __class_ptr), so the
# recursion is fixed-size and instance state survives — previously the
# field was truncated to a bare {__class_tag} placeholder and attribute
# chains through it went nondet.
from typing import Optional


class Node:
    def __init__(self, head: int, tail: Optional["Node"]):
        self.head = head
        self.tail = tail


a = Node(1, None)
b = Node(2, a)
c = Node(3, b)
assert c.head == 3
assert c.tail.head == 2
assert c.tail.tail.head == 1
