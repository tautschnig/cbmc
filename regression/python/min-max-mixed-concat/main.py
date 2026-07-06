# PLR §6.3.2/§3.2: `xs = [8,8]; xs = xs + ["c"]` -- the concat folds to a promoted
# list[python_value] literal, and the reassignment RETYPES the binding (rather
# than casting back to list[int] and dropping the str), so min()/max()/sorted()
# over the mixed-orderable-category result raises TypeError as in CPython.
# Previously KNOWNBUG (rand_169 family); now caught. CPython: TypeError.
xs = [8, 8]
xs = xs + ["c"]
r = min(xs)
