# Twin: deleting a key through a call is VISIBLE to the caller — the
# subsequent read raises KeyError (CPython ground truth).
d = {"x": 1, "y": 2}


def rm(p) -> None:
    del p["x"]


rm(d)
v = d["x"]
