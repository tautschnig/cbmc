# PLR §6.4.6 / Python re module: search/match/fullmatch raise
# TypeError when called with a non-string subject.
#
# The frontend statically types each subscript / dict / list and
# emits a dedicated 'type-error' property at any
# .search/.match/.fullmatch call whose first argument has a
# concrete non-string type. This catches typos in benchmarks
# that pass dicts/lists/etc. by accident and that an
# over-approximating regex model would otherwise discharge
# silently.

import re

# Should detect: dict argument.
d = {"a": 1, "b": 2}
re.compile("^x").search(d)
