# --python-check-annotations: storing a value incompatible with a dict's
# CONCRETE value-element annotation (d: dict[str,int]; d[k] = "s") is an
# annotation mismatch -- the dict-store analog of the list-append check.
# Opt-in (legal at runtime; the TypeError arises on a later use).
d: "dict[str, int]" = {}
d["k"] = "s"
