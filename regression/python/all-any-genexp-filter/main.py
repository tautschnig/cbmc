# PLR §6.2.4: 'all'/'any' over a generator expression with an
# 'if' filter clause. The generator only yields when the
# filter predicate holds; for filtered-out elements all()
# vacuously succeeds and any() doesn't contribute.

def all_with_filter() -> None:
    l = [1, -2, 3]
    # Only positive values are checked; all are > 0.
    assert all(x > 0 for x in l if x > 0) == True


def all_filter_empty() -> None:
    l = [1, 2, 3]
    # No element passes the filter → vacuous truth.
    assert all(x > 10 for x in l if x > 10) == True


def any_with_filter() -> None:
    l = [1, 2, -3, 4]
    # Only -3 passes the filter; -3 < 0 is True.
    assert any(x < 0 for x in l if x < 0) == True


def any_filter_empty() -> None:
    l = [1, 2, 3]
    # No element passes the filter → False.
    assert any(x > 10 for x in l if x > 10) == False


all_with_filter()
all_filter_empty()
any_with_filter()
any_filter_empty()
