# Write-through-box (PLR §3.1 + §6.4.6): d[k] = v through a BOXED dict
# (an untyped parameter, an iteration variable) REPLACES a present key
# and INSERTS an absent one, visible to the caller — the boxed-write
# arm was replace-only, so a new key silently vanished (readback
# KeyError: the untyped-param / iteration-variable false-alarm family,
# previously shared with the bounded model for the param case).
from typing import Any, List, Dict

d = {"x": 1}


def put(p) -> None:
    p["z"] = 3
    p["x"] = 7


put(d)
assert d["z"] == 3
assert d["x"] == 7
assert len(d) == 2


def tag(items: List[Dict[str, Any]]) -> None:
    for item in items:
        item["tagged"] = 1


xs = [{"n": "a"}, {"n": "b"}]
tag(xs)
assert xs[0]["tagged"] == 1
assert xs[1]["tagged"] == 1

# nested dicts: iteration + inner reads (the typecast8 crash class)
m = {"a": {"b": 1}}
for k1, inner in m.items():
    for k2, v in inner.items():
        assert v == 1
