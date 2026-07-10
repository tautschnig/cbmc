# PLR §6.10: tuple.index must work on ANY tuple-typed receiver, not just a
# resolved constant literal. A self-assigned / computed tuple (`t = t`,
# `t[0:1]*2`) is not tracked in tuple_literals, so the handler used to skip the
# index logic and never raise -- a false proof (mutation-oracle). index() now
# reads the fixed-arity TYPE's components via member access. Here 9 is absent, so
# ValueError is raised.
t = (0, 1, 0)
t = t
r = t.index(9)
