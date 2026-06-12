# Regression: imported-module function return-type inference must preserve
# None for an X-or-None return (shared infer_return_type_from_body, not the
# old partial scan that coerced None away). See plans section 0.
from mymod import maybe

assert maybe(False) is None
assert maybe(True) is not None
