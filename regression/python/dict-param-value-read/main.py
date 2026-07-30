# PLR §6.3.2 / §6.4.6: reading a VALUE out of a dict that crossed an
# untyped call boundary (the boto3-stub `params` shape). The boxed dict
# is canonicalized to dict[str, python_value] at wrap time; both the
# subscript read and .get() dereference __class_ptr at that layout and
# scan keys by string content. Previously BOTH reads were opaque
# nondet, so every downstream comparison was vacuous (false alarms
# against allow-lists; silently-untested postconditions).
def read_subscript(params=None):
    return params["Bucket"]


def read_get(params=None):
    return params.get("Bucket")


b1 = read_subscript(params={"Bucket": "data-service", "Key": "k"})
b2 = read_get(params={"Bucket": "data-service", "Key": "k"})
assert b1 == "data-service"
assert b2 == "data-service"
assert b1 != "auth-service"
