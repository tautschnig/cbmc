import re

# Soundness guard: a compilation flag (here IGNORECASE) changes the match
# semantics and is NOT modelled by the SMT regex translation. The model must
# therefore NOT commit to the flag-free (case-sensitive) decision -- doing so
# would be an unsound false proof. re.search("abc", "ABC", re.IGNORECASE)
# matches in CPython, so the model must keep that outcome reachable; this
# assertion that it does NOT match must be falsifiable (VERIFICATION FAILED).
m = re.search("abc", "ABC", re.IGNORECASE)
assert m is None
