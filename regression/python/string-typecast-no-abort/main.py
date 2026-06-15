# Defensive net: a wrong-typed coercion that puts an SMT String on one side of
# a typecast (the other side a bit-vector) must NOT abort the back-end. It used
# to hit PRECONDITION(false) in smt2_convt::convert_typecast. Here an empty
# (int-defaulted) list has strings appended in a loop with call-derived slice
# indices -- a shape that append-inference does not catch and that has no list
# annotation -- so the element coercion is smt_string -> signedbv. The back-end
# now emits a sound nondet of the destination sort instead of crashing.
result = []
string = "a12b34"
pos = 0
n = len(string)
while pos <= n:
    st = __cbmc_re_search_start("[0-9][0-9]", string, pos)
    if st < 0:
        break
    en = __cbmc_re_search_end("[0-9][0-9]", string, pos)
    result.append(string[st:en])
    if en > pos:
        pos = en
    else:
        pos = pos + 1
# Sound over-approximation: the run completes without aborting.
assert len(result) >= 0
