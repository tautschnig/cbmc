# PLR: enumerate(iterable, start=0) requires the iterable, so enumerate()
# raises TypeError at runtime. The frontend models this as an uncaught
# exception (arity validation), rather than verifying SUCCESSFUL.
for i, x in enumerate():
    pass
