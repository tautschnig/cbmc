def mutate(d):
    d["k"] = 9


m = {"k": 1}
mutate(m)
# Soundness guard: CPython has m["k"] == 9 after the by-reference mutation,
# so this assertion is FALSE and must NOT verify. Before the by-reference
# propagation fix this wrongly verified SUCCESSFUL (the mutation through the
# Any-typed parameter was lost and the read folded against the stale value).
assert m["k"] == 1
