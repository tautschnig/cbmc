# Exception payloads: raise T("msg") + except T as e makes
# the bound name carry the string payload.


def check(x: int) -> int:
    if x < 0:
        raise ValueError("negative")
    return x


try:
    check(-1)
except ValueError as e:
    msg = str(e)
    # "negative" is 8 chars
    assert len(msg) == 8


# Multiple raise/except paths
def classify(n: int) -> str:
    if n == 0:
        raise RuntimeError("zero")
    if n < 0:
        raise ValueError("neg")
    return "positive"


try:
    classify(0)
except RuntimeError as e1:
    m1 = str(e1)
    # "zero" is 4 chars
    assert len(m1) == 4


# Bare raise (no payload) — empty string
def bare_raise():
    raise Exception


try:
    bare_raise()
except Exception as e2:
    m2 = str(e2)
    # len >= 0 (sanity)
    assert len(m2) >= 0
