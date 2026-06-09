# Soundness: reading an attribute through a None tail must NOT verify
# (AttributeError on NoneType), i.e. the recursive-field lowering does not
# fabricate instance state for the None case.
from typing import Optional


class Node:
    def __init__(self, head: int, tail: Optional["Node"]):
        self.head = head
        self.tail = tail


a = Node(1, None)
assert a.tail.head == 99
