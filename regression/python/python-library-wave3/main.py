# Smoke test for Python-only library stubs: typing, contextlib,
# copy, heapq, bisect, operator, textwrap. Goal: each module
# imports and its most-used symbols are resolvable.
# Behavioural verification is out of scope — these are
# over-approximating wrappers.

# typing — just import; the @Generic/@Protocol classes are
# resolved at class-definition time.
from typing import List, Dict, Optional, Any, TYPE_CHECKING

# contextlib — trivial smoke.
from contextlib import nullcontext, suppress, ExitStack

# copy — identity model.
from copy import copy, deepcopy
a = [1, 2, 3]
b = copy(a)
c = deepcopy(a)

# heapq — import only.
from heapq import heappush, heappop, heapify, nlargest, nsmallest

# bisect — import only.
from bisect import bisect_left, bisect_right, insort

# operator — the simple ops work directly.
from operator import add, sub, mul
assert add(2, 3) == 5
assert sub(10, 4) == 6
assert mul(3, 4) == 12

# textwrap — import only.
from textwrap import wrap, fill, dedent
