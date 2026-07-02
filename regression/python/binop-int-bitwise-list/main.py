# PLR §6.10.1 / §3.2: a bitwise/shift operator is valid only between two ints or
# two sets, so `int <op> list` is a TypeError. A real list operand (no
# #python_set_semantic tag) combined with an int-like operand now fires.
# CPython: TypeError; expected: VERIFICATION FAILED.
r = 1 & [1]
