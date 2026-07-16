# CORE companion: an UNGUARDED sys.argv[1] may IndexError (argv length is
# nondet, matching a run with no arguments) -- must not verify.
import sys

x: str = sys.argv[1]
