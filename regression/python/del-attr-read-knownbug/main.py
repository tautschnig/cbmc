# KNOWNBUG (PLR §7.5 / §3.3.2): `del c.a` removes the instance attribute, so a
# subsequent read raises AttributeError (no __getattr__). The frontend resets the
# slot to the None marker instead, false-proving SUCCESSFUL. Sound fix: per-attr
# deleted-state tracking (whole-group with del-name; see plan).
class C:
    def __init__(self):
        self.a = 1


c = C()
del c.a
y = c.a
