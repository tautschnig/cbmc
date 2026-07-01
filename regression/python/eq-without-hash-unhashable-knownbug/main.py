# KNOWNBUG: a class that defines __eq__ but not __hash__ has __hash__ set to None
# (PLR §3.3.1), so its instances are UNHASHABLE -- `{C()}` raises TypeError. The
# frontend treats the instance as hashable. Desired: VERIFICATION FAILED.
class C:
    def __eq__(self, o) -> bool:
        return True


s = {C()}
