# Stage 1 of the re-precision plan: emit a 'regex-no-match'
# property at the call site when a regex with a constant pattern
# that can't match the empty string is applied to a statically
# empty subject.

import re

# Form A: re.search direct call.
re.search("^arn:aws", "")

# Form B: compile().search().
re.compile("^[a-z]+$").search("")

# Form C: re.compile().match() — same logic.
re.compile("^foo").match("")

# Form D: subject from a Name with recorded string constant.
empty: str = ""
re.search("^x", empty)
