# PLR §3.2: an UNTYPED empty list (`xs = []`) whose appended element type cannot
# be inferred (here a call result) gets element type python_value (Any), NOT a
# concrete int -- a concrete-int default would PUN a non-int store (cast to int)
# and let isinstance(xs[0], int) fold to true (a false proof). With the Any
# element the str tag is preserved, so isinstance(xs[0], int) is genuinely False.
# CPython: xs[0] is "x" -> isinstance False -> AssertionError. Expected: FAILED.
# (Contrast slot-pun-list-element-knownbug: an ANNOTATED `list[int]` still puns
# by design -- annotation-laundering, intrinsic.)
def src():
    return "x"


xs = []
xs.append(src())
assert isinstance(xs[0], int)
