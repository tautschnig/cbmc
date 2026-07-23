# KNOWNBUG (false PROOF residual): bool(Decimal("0")) is False, so
# `assert not Decimal("0")` must hold and `assert Decimal("0")` must FAIL.
# DESIRED: VERIFICATION FAILED. CURRENT: VERIFICATION SUCCESSFUL -- the
# Decimal(<literal>) construction fold does not fire through the
# `from decimal import Decimal` binding (the name binds the class object,
# __class_tag 11), so __bool__ reads a nondet _int. Needs the Decimal-model
# construction pass (see doc/python-frontend-decimal-plan.md section 9).
# When fixed -> promote to CORE.
from decimal import Decimal

d = Decimal("0")
assert d  # WRONG: a zero Decimal is falsy, so this assert must fail
