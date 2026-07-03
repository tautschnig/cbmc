# KNOWNBUG (PLR §7.5 / §4.2.2): deleting a variable captured by a nested function
# then calling that function raises NameError (CPython captures by CELL, so `del`
# unbinds the shared cell). The frontend captures the free variable BY VALUE, so
# the closure reads a stale copy unaffected by `del` -> false-proves SUCCESSFUL.
# The direct-read del case (`del x; x`) IS caught (del-name-use CORE); this
# closure sub-case needs the by-reference cell-capture model (fat-closure plan).
def f():
    x = 5

    def g():
        return x

    del x
    return g()


f()
