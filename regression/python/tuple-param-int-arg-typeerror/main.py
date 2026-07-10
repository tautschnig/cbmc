# CORE soundness guard: adding TUPLE to the subscriptable set must NOT make a
# genuinely non-subscriptable receiver pass. An int arg to a `t[0]` param still
# raises TypeError ('int' object is not subscriptable). CPython raises -> FAILED.
def f(t):
    return t[0]


f(5)
