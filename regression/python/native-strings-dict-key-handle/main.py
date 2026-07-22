# Dict string KEYS are string-id handles under the native backend
# (python_dict_key_elem_type). The earlier POINTER box kept the dict
# struct fixed-width but the pointed smt_string was byte-imaged whenever
# the key pointer entered an unresolved value-set deref (a nondet str
# parameter's truthiness enumerating the key heap objects -- the
# apigateway unpack_rec abort). A handle has no pointee, so nothing
# byte-granular is reachable. Also pins the pv->string-slot rule in
# coerce_element: a python_value key coerces through its STRING
# denotation, not __int_val (the csv _dialects.pop type-mismatch crash
# -- the Any-key pop below must CONVERT and verify, not abort).
# Interprocedural pop membership precision is a recorded residual; this
# test pins the crash-free conversion + the direct-shape precision.
from typing import Any


def store(name: str, description: str = None) -> dict:
    params: dict = {"name": name, "enabled": True}
    if description:
        params["description"] = description
    return params


def drop_any(d: dict, key: Any) -> None:
    d.pop(key, None)


p = store("k", "text")
assert p["name"] == "k"
assert "description" in p
p.pop("description", None)
assert "description" not in p
drop_any(p, "name")
assert True
