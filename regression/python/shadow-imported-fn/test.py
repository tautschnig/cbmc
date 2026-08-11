# Regression: when a local variable name happens to match a function
# the import pipeline added at the same qualified scope (e.g.
# `match: ... = re.match(...)` reusing `python::match`), the variable
# must be shadow-renamed rather than reusing the existing code-typed
# symbol — otherwise CBMC's symex aborts with
# "assignment to 'symbol' not handled".

import re
s: str = "foo"
match: re.Match[str] | None = re.match(r"foo", s)
if match is not None:
    pass
