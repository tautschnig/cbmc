# dict.fromkeys default-value path: the value local was a
# DEFAULT-CONSTRUCTED exprt (empty id -- is_nil() false), so the None
# default never applied and an EMPTY expression flowed into the dict
# pairs (a boolbv "empty type" abort -- the ESBMC-suite
# dict_eq_none_list crashes). Now nil-initialised; both forms pinned.
d = dict.fromkeys([1, 2, 3])
assert len(d) == 3
assert d[1] is None

e = dict.fromkeys([1, 2], 7)
assert e[2] == 7
