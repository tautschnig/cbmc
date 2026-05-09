# Smoke test for Python-only library stubs: dataclasses,
# argparse, csv, hashlib, itertools. Goal: each module parses,
# resolves, and its core symbols are callable. Behavioural
# verification is out of scope — these are over-approximating
# wrappers.

from dataclasses import dataclass, MISSING, is_dataclass

@dataclass
class Point:
    x: int = 0
    y: int = 0

# @dataclass is an identity decorator in our stub, so Point's
# default __init__ leaves x and y at their declared defaults.
p = Point()
assert p.x == 0
assert p.y == 0

# argparse: parser construction and add_argument smoke.
from argparse import ArgumentParser, SUPPRESS
parser = ArgumentParser(description="test")
parser.add_argument("--verbose", default=SUPPRESS)

# csv: writer smoke — writerow on a nondet output.
from csv import writer, QUOTE_MINIMAL
w = writer([])
w.writerow(["a", "b"])

# hashlib: constructor + update + hexdigest.
from hashlib import sha256
h = sha256()
h.update(b"hello")
_ = h.hexdigest()

# itertools: chain smoke test (generator is not iterated — just
# ensure the symbol resolves).
from itertools import chain, pairwise, accumulate
_ = chain
_ = pairwise
_ = accumulate
