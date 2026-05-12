# PLR §7.8: bare `raise` inside an except handler
# re-raises the currently active exception without
# changing its type. Previously we overwrote
# __exception_type with the hash of "Exception" so
# the outer except TypeError failed to match.

caught = [0]
try:
    try:
        raise TypeError("t")
    except TypeError:
        raise  # re-raise current
except TypeError:
    caught[0] = 1

assert caught[0] == 1
