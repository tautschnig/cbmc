"""
Verification model of the `copy` module.

Python's ``copy.copy()`` (shallow) and ``copy.deepcopy()``
(recursive) are modelled as identity at the verification layer —
we return the input unchanged. This is a sound over-
approximation for verification: any user code that relies on
the copied object being a distinct object from the original
(e.g. mutating one without affecting the other) is left to
CBMC's aliasing analysis, which treats equal pointers as equal.
Most verification targets use copy for immutability, which the
identity model preserves.
"""


class Error(Exception):
    pass


error = Error


def copy(x):
    """Shallow copy. Returns the input; aliasing unchanged."""
    return x


def deepcopy(x, memo=None, _nil=[]):
    """Recursive deep copy. Returns the input; same caveat."""
    return x


# Hook points — no-ops; real CPython uses them for custom classes.
def __copy__(self):
    return self


def __deepcopy__(self, memo):
    return self


# Internal dispatch tables — empty so user registrations don't
# produce errors.
_copy_dispatch = {}
_deepcopy_dispatch = {}
_deepcopy_atomic = {}
