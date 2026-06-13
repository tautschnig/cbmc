class Recorder:
    # A user class that defines a method whose name collides with a built-in
    # list method. On an Any-typed receiver this must dispatch to the class
    # method (virtual dispatch), NOT be unwrapped as a list.
    def append(self, v):
        self.last = v


def use(o):
    o.append(5)


r = Recorder()
use(r)
assert r.last == 5
