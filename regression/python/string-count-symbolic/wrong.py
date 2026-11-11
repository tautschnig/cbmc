# Beyond 8 occurrences the count is only bounded: neither this assertion
# nor its negation is proved.
u = nondet_str()
if u == "aaaaaaaaaaaa":
    assert u.count("a") == 12
