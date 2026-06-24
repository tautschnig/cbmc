# A dict literal captures its values BY VALUE at construction. The dict-literal
# constant-fold must not re-read a value that embeds a mutable variable: here d
# captures n's value (100) at construction, so d["k"] is 100 even after n is
# reassigned to 200. Before the fix the const-fold returned the tracked
# expression `n`, which symex renamed to the latest value (200) -- a false proof
# (d["k"] == 200 wrongly verified). The fix (value_is_const_foldable) skips the
# const-fold for values embedding a mutable lvalue, falling through to the sound
# per-construction symbolic read.

n = 100
d = {"k": n}
n = 200
assert d["k"] == 100
assert d["k"] != 200
