# string_to_handle representation guard: a NON-smt_string value flowing
# into a handle slot (a bytes-returning stub method stored into a
# str-typed slot -- io.read via nondet_bytes) allocates a handle with an
# UNCONSTRAINED strtab image instead of emitting a sort-mismatched
# strtab(h) == <struct> axiom (the s3_to_dynamodb value-set crash).
# Sound over-approximation: nothing provable about the value, no crash,
# and the surrounding program still verifies.
import io

buf = io.BytesIO(b"xy")
d = {"data": buf.read()}
assert "data" in d
assert len(d) == 1
