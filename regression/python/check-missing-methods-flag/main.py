# --python-check-missing-methods (static-strictness family, included in
# --python-strict): a call to a method not declared anywhere on the
# receiver's class (MRO-aware; no __getattr__ / dynamic attrs) is flagged
# EVEN when an enclosing handler catches the AttributeError. Default mode
# stays PLR §8.4-faithful: the caught AttributeError is real control flow
# (try/except probing is a Python idiom), so no property fires there --
# both modes pinned by the two test.desc files. Found by the corpus MISS
# study: rds_instance_creator's create_dbinstance typo (for
# create_db_instance) was swallowed by its generic `except Exception`.
class Client:
    def create_db_instance(self, **kwargs) -> int:
        return 1


c = Client()
try:
    r = c.create_dbinstance(X=1)
except Exception:
    r = 0
assert r == 0
