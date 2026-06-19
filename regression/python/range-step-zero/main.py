# PLR §6.10.2: range() with a zero step raises ValueError ("range() arg 3
# must not be zero"), for a literal or a symbolic step. Modelled as an
# uncaught exception.
step = 0
for i in range(1, 5, step):
    pass
