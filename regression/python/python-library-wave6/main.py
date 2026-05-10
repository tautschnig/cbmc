# Wave 6: decimal, pathlib, urllib.parse, json imports.
# Smoke-tests that each module loads and exposes its
# interface.

# decimal
from decimal import Decimal, ROUND_HALF_EVEN, getcontext
d = Decimal(10)
e = Decimal(5)
s = d + e
# Decimal arithmetic short-circuited through floats.

# pathlib
from pathlib import Path
p = Path("/tmp")

# urllib.parse
from urllib.parse import urlparse, urlencode

# json
from json import dumps, loads
