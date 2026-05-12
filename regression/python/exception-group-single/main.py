# PEP 654 (Python 3.11): ExceptionGroup / except*.
# Our frontend handles the common case of a
# single-element ExceptionGroup literal, treating it
# as raising the contained exception's type.
# Multi-element groups are over-approximated: the
# first element's type is raised. See
# doc/remaining-precision-items.md for the full
# state-machine approach.

caught = [0]

try:
    raise ExceptionGroup("grp", [ValueError("v")])
except* ValueError:
    caught[0] = 1

assert caught[0] == 1
