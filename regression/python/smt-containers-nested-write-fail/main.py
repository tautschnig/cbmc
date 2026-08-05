# Twin: the value written through the box must be the REAL value.
d = {"x": 1}


def put(p) -> None:
    p["z"] = 3


put(d)
assert d["z"] == 4
