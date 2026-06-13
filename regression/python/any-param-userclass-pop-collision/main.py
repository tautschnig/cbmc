class Stack:
    # A user class defining a method whose name collides with the built-in
    # ambiguous container method `pop`. On an Any receiver this must dispatch
    # to the class method, not the runtime container __tag dispatch.
    def pop(self):
        self.popped = True


def use(o):
    o.pop()


s = Stack()
use(s)
assert s.popped == True
