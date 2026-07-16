# CORE: sys.argv is modelled as a nondet list of strings (library/sys.py). A
# length-guarded access is precise: on the guarded path len(sys.argv) == 2
# constrains the SAME symbol's length, so argv[1] is in-bounds and a str.
# Requires (a) the module-attribute read returning the registered stub
# global's LVALUE (not a nondet attribute), (b) nondet_* initialisers
# registering valueless (nondet) typed globals, (c) the bounded-container
# assumption at the read.
import sys

if len(sys.argv) == 2:
    x: str = sys.argv[1]
    assert isinstance(x, str)
assert len(sys.argv) >= 0
