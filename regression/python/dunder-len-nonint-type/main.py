# PLR §3.3.1: __len__ must return an integer. A concretely non-int return type
# (here str) raises TypeError at the len() call. The negative-VALUE case
# (__len__ returning a negative int) is a separate, value-dependent check kept
# in dunder-len-nonint-knownbug.
class C:
    def __len__(self):
        return "x"


n = len(C())
