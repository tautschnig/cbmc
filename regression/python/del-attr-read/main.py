# PLR §7.5/§6.10: `del c.a` on an INSTANCE-ONLY attribute (no class-level default)
# removes it; a subsequent read raises AttributeError. Modeled by a per-instance
# __present_<attr> bool flag (set on store, cleared on del, checked on read).
# Per-instance + by-reference so aliasing is handled. CPython: AttributeError.
class C:
    def __init__(self):
        self.a = 5


c = C()
del c.a
x = c.a
