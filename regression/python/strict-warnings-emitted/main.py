# --python-strict-warnings promotes the frontend's over-approximation log from
# debug to warning level. An attribute read on an opaque base over-approximates.
def f(x):
    return x.attr

import sys
y = f(sys)
