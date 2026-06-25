# Soundness: the set is a 64-bit int bitmap; a non-int element (tuple) cast to a
# bit position used to COLLIDE and false-prove membership. Adding (1,2) must NOT
# make (3,4) a member. The membership of a non-int element is modelled nondet,
# so this genuinely-false assertion must produce VERIFICATION FAILED.
s = set()
s.add((1, 2))
assert (3, 4) in s
