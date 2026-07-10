# PLR §4.2.2/§7.5: deleting a variable captured by a nested function then calling
# that function raises NameError (the captured cell is unbound). The frontend
# implements the capture by passing the enclosing variable as a call argument;
# that capture-read is now guarded by the enclosing scope's per-name deleted flag.
# CPython: NameError; expected: VERIFICATION FAILED.
def f():
    x = 5

    def g():
        return x

    del x
    return g()


f()
