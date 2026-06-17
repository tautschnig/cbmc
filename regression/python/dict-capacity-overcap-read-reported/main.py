# A dict's key scan is bounded by the modelled capacity PYTHON_MAX_DICT_SIZE
# (16), so an over-capacity dict (here a 20-entry literal) would silently MISS
# entries at indices >= 16 (a bogus KeyError / wrong value). The over-capacity
# literal is now reported as a python-model-bound violation at CONSTRUCTION and
# the path cut (covering every downstream read and iteration), instead of being
# silently mis-modelled. Raising --max-dict-size would clear it.
d = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 9,
     10: 10, 11: 11, 12: 12, 13: 13, 14: 14, 15: 15, 16: 16, 17: 17,
     18: 18, 19: 19}
x = d[5]
