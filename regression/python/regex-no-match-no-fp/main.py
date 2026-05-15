# Negative tests: regex CAN match the empty subject, OR pattern
# is unanalyzable. The regex-no-match property should NOT fire.

import re

# Pattern that accepts ε.
re.search("^a*$", "")
re.search("^(a|)$", "")
re.search("^(a|b*)$", "")
re.search("^a?b?$", "")
re.search("^a{0,3}$", "")

# Empty subject, but pattern uses back-reference (unsupported by
# the analyser — we conservatively don't fire).
re.search(r"(\w+)\1", "")

# Non-empty subject — even if the pattern can't match empty, the
# subject isn't empty, so the static check shouldn't fire.
re.search("^arn:", "arn:foo")
