# KNOWNBUG (false PROOF, from laurel-encoding-experiments K):
# `isinstance(entry, str)` is proved although the NotRequired key
# may be ABSENT on one branch, making entry None there. The
# branch-merged .get result loses the None arm. DESIRED:
# VERIFICATION FAILED. Isolated from the study's property_fails.py.
from typing import NotRequired, TypedDict


class AthenaOut(TypedDict, total=False):
    """The TypedDict. `total=False` plus NotRequired: the key MAY be absent."""
    QueryExecutionId: NotRequired[str]


def nondet_bool() -> bool: ...


class Session:
    """The class: heap identity plus mutation."""

    def __init__(self, region: str) -> None:
        self.region = region
        self.call_count = 0

    def bump(self) -> None:
        self.call_count += 1


SESSION = Session(region="us-east-1")

# The boundary call, as the three-way nondeterministic choice the Strata contracts
# state: shaped-with-key, shaped-without-key, or an error carried as data.
SESSION.bump()
if nondet_bool():
    response = AthenaOut(QueryExecutionId="qid-sales_db")
    is_error = False
elif nondet_bool():
    response = AthenaOut()          # legal: the NotRequired key is absent
    is_error = False
else:
    response = AthenaOut()
    is_error = True

# The dispatch, matching the comprehension entry in ../program_under_test.py:
#     r.get('QueryExecutionId') if isinstance(r, dict) else str(r)
if is_error:
    entry = "throttled"
else:
    entry = response.get('QueryExecutionId')

# THE FAILING QUERY: every value is a str. FALSE on the absent-key branch.
assert isinstance(entry, str)
