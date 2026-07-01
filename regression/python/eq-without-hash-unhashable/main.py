# PLR §3.3.1: a class whose OWN body defines __eq__ but not __hash__ has __hash__
# implicitly set to None, so its instances are UNHASHABLE -- using one as a set
# element (or dict key) raises TypeError. is_unhashable_type (the shared
# hashability lever, used at set-literal / dict-literal / d[k]= / set.add /
# comprehension sites) now covers such classes. CPython: TypeError; expected:
# VERIFICATION FAILED.
class C:
    def __eq__(self, o) -> bool:
        return True


s = {C()}
