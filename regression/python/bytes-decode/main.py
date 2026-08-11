# PLR §4.6: bytes.decode(encoding) returns a str. For
# constant bytes the converter folds to a python_string
# literal so subsequent string-solver operations see the
# real content.

def basic_decode() -> None:
    b = b"hello"
    s = b.decode("utf-8")
    assert len(s) == 5
    assert s == "hello"


def decode_via_var() -> None:
    bs = b"abc"
    s = bs.decode("utf-8")
    assert s == "abc"


def decode_empty() -> None:
    b = b""
    s = b.decode("utf-8")
    assert len(s) == 0
    assert s == ""


basic_decode()
decode_via_var()
decode_empty()
