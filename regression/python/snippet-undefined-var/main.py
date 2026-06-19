# PLR §4.2.1: `x` is bound nowhere in the module, so reading it raises
# NameError at runtime. The frontend detects this (the name is absent from
# the AST-server-computed all_bound_names set) and models it as an uncaught
# exception, rather than silently resolving it to a nondet value.
y = x + 3
assert y > 0
